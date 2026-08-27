//
// RTI-Zone Dome Rotator firmware.
// for ESP32
//
//  Copyright © 2024 Rodolphe Pineau. All rights reserved.
//

//
// Board Settings (Tools menu):
// --------------------------------
// Board:            ESP32 Dev Module
// Flash Size:       4MB
// Partition Scheme: Minimal SPIFFS (1.9MB APP with OTA / 128KB SPIFFS)
// --------------------------------
//

// Uncomment #define DEBUG in config.h to enable printing debug messages on serial port defined as DebugPort

#include "Arduino.h"
#include <rtc_wdt.h>
#include <esp_task_wdt.h>
#include "config.h"

bool firstLoop = true;
#include "RotatorClass.h"

#pragma message "Ethernet enabled"
// include and some defines for ethernet connection
// #include <SPI.h>    // ESP32 :  SCK: GPIO18, SDO/TX: GPIO23, SDI: GPIO19, CS: GPIO5, Reset : GPIO29, Int : GPIO0
// #include <Ethernet.h>
#include <Network.h>

#ifdef USE_OTA_UPDATE
#pragma message "OTA Update enable"
#include <WebServer.h>
#include <HTTPUpdateServer.h>
#endif

byte MAC_Address[6];

IPConfig ServerConfig;
volatile bool ethernetPresent = false;
NetworkServer *domeServer = nullptr;
NetworkClient domeClient;
int nbNetworkClient = 0;
String networkBuffer = "";
String sLocalIPAdress = "";
// OTA update stuff
#ifdef USE_OTA_UPDATE
WebServer httpServer(OTA_PORT);
HTTPUpdateServer httpUpdater;
#endif

#ifdef USE_WIFI
#pragma message "Local WiFi shutter enable"
#include "RemoteShutterClass.h"
#include <WiFi.h>
#include <WiFiAP.h>
volatile bool wifiPresent = false;
WIFIConfig wifiConfig;
WiFiServer *shutterServer = nullptr;
WiFiClient shutterClient;
String wifiBuffer = "";
int nbWiFiClient = 0;
String sLocalWifiIPAddress;
volatile bool bGotHelloFromShutter = false;
RemoteShutterClass RemoteShutter;
#endif

String computerBuffer = "";

bool bGlobalParked = false; // use to the run check doesn't continuously try to park

RotatorClass *Rotator = NULL;

static const unsigned long pingInterval = 5000; // 5 seconds, can't be changed with command

// Once booting is done and wifi is ready, broadcast a hello message
// so a shutter knows you're around if it is already running. If not,
// the shutter will send a hello when it boots.
volatile bool bSentHello = false;

#ifdef USE_WIFI
// Timer to periodically ping the shutter
StopWatch PingTimer;
StopWatch ShutterWatchdog;
#endif

volatile bool bShutterPresent = false;
// global variable for condition status
volatile bool bIsSafe{true};
// global variable for shutter voltage state
volatile bool bLowShutterVoltage = false;

// shutter button on rotation controller
volatile bool bOpenShutterButtonPressed = false;
volatile bool bCloseShutterButtonPressed = false;

const char ERR_NO_DATA = -1;

#include "dome_commands.h"
enum CmdSource {SERIAL_CMD, NETWORK_CMD};
// function prototypes
void configureEthernet();
bool initEthernet(bool bUseDHCP, IPAddress ip, IPAddress dns, IPAddress gateway, IPAddress subnetMask, bool bReconfigure);
void checkForNewTCPClient();
#ifdef USE_WIFI
void configureWiFi();
bool initWiFi(IPAddress ip, String sSSID, String sPassword);
void checkForNewWifiClient();
#endif
void homeIntHandler();
void conditionsIntHandler();
void buttonWestHandler();
void buttonEastHandler();
void openShutterButtonInt();
void closeShutterButtonInt();
void resetChip(int);
void StartWirelessConfig();
void ConfigXBee();
void SendHello();
void requestShutterData();
void CheckForCommands();
void CheckForConditions();
void ReceiveNetwork(NetworkClient client);
void ReceiveComputer();
void ProcessCommand(int nSource);
#ifdef USE_WIFI
void checkShuterLowVoltage();
void ReceiveWiFi(WiFiClient client);
void ProcessWifi();
void PingWiFiShutter();
void requestWiFiShutterData();
#endif
void Abort();

#ifdef USE_ALPACA
#include "AlpacaAPI.h"
DomeAlpacaServer *AlpacaServer;
DomeAlpacaDiscoveryServer *AlpacaDiscoveryServer;
#endif

void MotorTask(void *);
esp_task_wdt_config_t twdt_config = {
	.timeout_ms = 1000000,
	.idle_core_mask = 0,    // Bitmask of cores
	.trigger_panic = false,
};

//
// Setup and main loops
//
void setup()
{
#ifdef MOTION_LOG
	logMotion(resetReasonName(esp_reset_reason()));
#endif

#ifdef USE_WIFI
	nbWiFiClient = 0;
#endif
	nbNetworkClient = 0;

#ifdef DEBUG
#ifndef DEBUG_TO_COMPUTER
	DebugPort.begin(115200, SERIAL_8N1, 16, 17); // pins 16 rx2, 17 tx2, 115200 bps, 8 bits no parity 1 stop bit
	delay(1000);
	DBPrintln("========== RTI-Zone controller booting ==========");
#endif
#endif

	digitalWrite(ETHERNET_RESET, 0);
	pinMode(ETHERNET_RESET, OUTPUT);
	Computer.begin(115200);

	Rotator = new RotatorClass();
	Rotator->motorStop();
	Rotator->Stop();
	Network.begin();
	configureEthernet();

#ifdef USE_WIFI
	bSentHello = false;
	bGotHelloFromShutter = false;
	configureWiFi();
#endif

	esp_task_wdt_deinit();
	esp_task_wdt_init(&twdt_config);
	esp_task_wdt_add(NULL);
	disableCore0WDT();
	disableCore1WDT();
	xTaskCreatePinnedToCore(MotorTask, "MotorTask", 32768, NULL, 16, NULL,  0);

	domeServer = new NetworkServer(CMD_SERVER_PORT);
	domeServer->begin();

	attachInterrupt(SPARE1, openShutterButtonInt, FALLING);
	attachInterrupt(SPARE2, closeShutterButtonInt, FALLING);

#ifdef USE_OTA_UPDATE
	httpUpdater.setup(&httpServer);
	httpServer.begin();
#endif

#ifdef USE_ALPACA
	AlpacaDiscoveryServer = new DomeAlpacaDiscoveryServer();
	AlpacaDiscoveryServer->startServer();
	AlpacaServer = new DomeAlpacaServer();
	AlpacaServer->startServer();
#endif
	DBPrintln("========== Ready ==========");
}

//
// main loop task takes care of all communications and commands
//

void loop()
{
	String sTmpString;
	String ShutterAction;
	if(firstLoop) {
		firstLoop = false;
		Computer.println("========== Rotator is Ready ==========");
	}

	if(ethernetPresent) {
		checkForNewTCPClient();
		AlpacaDiscoveryServer->checkForRequest();
		AlpacaServer->checkForRequest();
	}

#ifdef USE_WIFI
	if(wifiPresent) {
		checkForNewWifiClient();
		if(nbWiFiClient && shutterClient.connected())
			checkShuterLowVoltage();
		if(ShutterWatchdog.elapsed() > (pingInterval*5)) {
			if(nbWiFiClient) {
				if(!shutterClient.connected()) {
					shutterClient.stop();
					nbWiFiClient--;
					bShutterPresent = false;
				}
			}
		}
		if(!bSentHello) {
			SendHello();
		}
		else {
			PingWiFiShutter();
		}
		if(bGotHelloFromShutter) {
			requestWiFiShutterData();
			bGotHelloFromShutter = false;
		}
	}
#endif // USE_WIFI

	CheckForCommands();
	CheckForConditions();
#ifdef USE_OTA_UPDATE
	httpServer.handleClient();
#endif
	if(bOpenShutterButtonPressed) {
		bOpenShutterButtonPressed = false;
		if(shutterClient.connected()) {
			if(RemoteShutter.state == OPENING) {
				sTmpString = String(ABORT); // we're stopping in the middle.
				RemoteShutter.state = ERROR;
			}
			else {
				sTmpString = String(OPEN_SHUTTER);
				RemoteShutter.state = OPENING; // force it
			}
			sTmpString = sTmpString+ "#";
			shutterClient.write(sTmpString .c_str(), sTmpString.length());
			vTaskDelay(DELAY_WIFI / portTICK_PERIOD_MS);
			ReceiveWiFi(shutterClient);
		}
	}
	if(bCloseShutterButtonPressed) {
		bCloseShutterButtonPressed = false;
		if(shutterClient.connected()) {
			if(RemoteShutter.state == CLOSING) {
				sTmpString = String(ABORT); // we're stopping in the middle.
				RemoteShutter.state = ERROR;
			}
			else {
				sTmpString = String(CLOSE_SHUTTER);
				RemoteShutter.state = CLOSING; // force it
			}
			sTmpString = sTmpString+ "#";
			shutterClient.write(sTmpString .c_str(), sTmpString.length());
			vTaskDelay(DELAY_WIFI / portTICK_PERIOD_MS);
			ReceiveWiFi(shutterClient);
		}
	}
	taskYIELD();
	esp_task_wdt_reset();
}

//
// This task does all the motor controls
//
void MotorTask(void *)
{
	const TickType_t xDelay = 50/ portTICK_PERIOD_MS; // 50ms task block to give time back
	DBPrintln("========== Motor task starting ==========");
	DBPrintln("========== MotorTask Priority " + String(uxTaskPriorityGet(NULL)) + " ==========");
	DBPrintln("========== Motor task Attaching interrupt handler ==========");

	attachInterrupt(HOME_PIN, homeIntHandler, FALLING);
	attachInterrupt(CONDITION_SENSOR_PIN, conditionsIntHandler, CHANGE);
	attachInterrupt(BUTTON_CW, buttonWestHandler, CHANGE);
	attachInterrupt(BUTTON_CCW, buttonEastHandler, CHANGE);

	esp_task_wdt_add(NULL);
	DBPrintln("========== Motor task ready ==========");

	for(;;) {
		Rotator->Run();
		taskYIELD();
		esp_task_wdt_reset();
		vTaskDelay(xDelay);
	}
}

//
//
//
void configureEthernet()
{
        DBPrintln("========== Configuring Ethernet ==========");
        Rotator->getIpConfig(ServerConfig);
        ethernetPresent =  initEthernet(ServerConfig.bUseDHCP,
										ServerConfig.ip,
										ServerConfig.dns,
										ServerConfig.gateway,
										ServerConfig.subnetMask);
}

#ifdef DEBUG
void onEvent(arduino_event_id_t event, arduino_event_info_t info)
{
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      DBPrintln("ETH Started");
      //set eth hostname here
      DBPrintln("esp32-eth0");
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      DBPrintln("ETH Connected");
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      DBPrintln("ETH Got IP: '" + String(esp_netif_get_desc(info.got_ip.esp_netif)) +"'");
      DBPrintln(ETH);
      break;
    case ARDUINO_EVENT_ETH_LOST_IP:
      DBPrintln("ETH Lost IP");
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      DBPrintln("ETH Disconnected");
      break;
    case ARDUINO_EVENT_ETH_STOP:
      DBPrintln("ETH Stopped");
      break;
    default:
      break;
  }
}
#endif
bool initEthernet(bool bUseDHCP, IPAddress ip, IPAddress dns, IPAddress gateway, IPAddress subnetMask)
{
	bool bDhcpOk;
	int nTimeout = 0;
#ifdef DEBUG
	Network.onEvent(onEvent); // this is just for debugging
#endif
	DBPrintln("========== Init Ethernet ==========");
	// resetChip(ETHERNET_RESET);
	SPI.begin(ETH_SPI_SCK, ETH_SPI_MISO, ETH_SPI_MOSI);
	// network configuration
	if(!ETH.begin(ETH_PHY_TYPE, ETH_PHY_ADDR, ETH_PHY_CS, ETH_PHY_IRQ, ETH_PHY_RST, SPI)) {
		DBPrintln("NO HARDWARE !!!");
		return false;
	}
	nbNetworkClient = 0;
	// set an ip so we can get the link status
	domeEthernet.config(ip, gateway, subnetMask);
	while(!domeEthernet.linkUp() ) {
		vTaskDelay(250 / portTICK_PERIOD_MS);
		nTimeout++;
		if(nTimeout == 120) { // 30 seconds timeout, 250ms per loop, 120 loops = 30 seconds
			return false;
		}
	}

	domeEthernet.macAddress(MAC_Address);
	domeEthernet.setHostname("RTI-Dome");

	DBPrintln("========== Setting IP config ==========");
	// try DHCP if set
	if(bUseDHCP) {
		bDhcpOk = domeEthernet.config(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0)); // all value set to the default 0 means use dhcp.
		if(bDhcpOk) {
			nTimeout = 0;
			while(domeEthernet.localIP() == IPAddress(0,0,0,0) ) {
				vTaskDelay(250 / portTICK_PERIOD_MS);
				nTimeout++;
				if(nTimeout == 120) { // 30 seconds timeout, 250ms per loop, 120 loops = 30 seconds
					break;
				}
			}
		}
	}
	else {
		domeEthernet.config(ip, gateway, subnetMask);
		domeEthernet.dnsIP(0,dns);
	}

	if(domeEthernet.localIP() == IPAddress(0,0,0,0)) {
			domeEthernet.config(ip, gateway, subnetMask); // use defaults
			vTaskDelay(250 / portTICK_PERIOD_MS);
	}

	domeEthernet.setDefault();

	DBPrintln("========== Checking hardware status ==========");
	DBPrintln("W5500 Ok.");
	DBPrintln("W5500 IP = " + RotatorClass::IpAddress2String(domeEthernet.localIP()));
#ifdef DEBUG
	char macBuffer[20];
	snprintf(macBuffer,20,"%02x:%02x:%02x:%02x:%02x:%02x",
		MAC_Address[0],
		MAC_Address[1],
		MAC_Address[2],
		MAC_Address[3],
		MAC_Address[4],
		MAC_Address[5]);
	DBPrintln("Dome MAC : " + String(macBuffer));
#endif

	DBPrintln("Server ready");

	sLocalIPAdress = RotatorClass::IpAddress2String(domeEthernet.localIP());
	return true;
}


void checkForNewTCPClient()
{
	if(!domeServer)
		return;

	NetworkClient newClient = domeServer->accept();
	if(newClient) {
		DBPrintln("new client");
		if(nbNetworkClient > 0) { // we only accept 1 client
			newClient.write("Already in use#");
			newClient.stop();
			DBPrintln("new client rejected");
		}
		else {
			nbNetworkClient++;
			domeClient = newClient;
			DBPrintln("new client accepted");
			DBPrintln("nb client = " + String(nbNetworkClient));
		}
	}

	if((nbNetworkClient>0) && !domeClient.connected()) {
		DBPrintln("client disconnected");
		domeClient.stop();
		nbNetworkClient--;
		DBPrintln("nb client = " + String(nbNetworkClient));
	}
}

#ifdef USE_WIFI
void configureWiFi()
{
	if(nbWiFiClient>0) {
		DBPrintln("Disconnect clients");
		shutterClient.stop();
		nbWiFiClient--;
	}

	DBPrintln("========== Configuring WiFi ==========");
	Rotator->getWiFiConfig(wifiConfig);
	if(Rotator->getSSID().length() < 1) {
		Rotator->setWifiDefault();
	}
	wifiPresent = initWiFi(wifiConfig.ip,
								String(wifiConfig.sSSID),
								String(wifiConfig.sPassword));
}

bool initWiFi(IPAddress ip, String sSSID, String sPassword)
{
	bool bWiFiOk = true;

	shutterWiFi.disconnect();
	shutterWiFi.mode(WIFI_AP);
	DBPrintln("WiFi SSID = " + sSSID);
	DBPrintln("WiFi Password = " + sPassword);

	bWiFiOk = shutterWiFi.softAP(sSSID.c_str(), sPassword.c_str());
	if(!bWiFiOk) {
		DBPrintln("Failled to configure WiFi");
		return false;
	}
	shutterWiFi.softAPConfig(ip, ip, IPAddress(255,255,255,0));
	shutterWiFi.setHostname("RTI-Dome");
	DBPrintln("WiFi IP = " + RotatorClass::IpAddress2String(WiFi.softAPIP()));

	if(shutterServer) {
		shutterServer->stop();
		delete shutterServer;
		shutterServer = nullptr;
	}
	shutterServer = new WiFiServer(ip,SHUTTER_PORT);
	if(!shutterServer) {
		DBPrintln("========== Failed to start shutterServer ==========");
		return false;
	}
	else {
		shutterServer->begin();
		shutterServer->setNoDelay(true);
	}
	shutterClient.stop();
	return true;

}

void checkForNewWifiClient()
{
	if(!shutterServer)
		return;

	if(nbWiFiClient>0 && !shutterClient.connected()) {
		DBPrintln("WiFi client disconnected");
		shutterClient.stop();
		nbWiFiClient--;
	}

	WiFiClient newClient = shutterServer->accept();
	if(newClient) {
		DBPrintln("new WiFi client");
		if(nbWiFiClient > 0) { // we only accept 1 client, disconnect previous one
			shutterClient.clear();
			shutterClient.stop();
		}
		nbWiFiClient++;
		shutterClient = newClient;
		shutterClient.setNoDelay(true);
		shutterClient.setTimeout(250);
		DBPrintln("new wiFi client accepted");
		SendHello();
	}
}

#endif

// no need for software debouncing as all input have hardware debouncing via a 10uF capacitor
void IRAM_ATTR homeIntHandler()
{
	if(Rotator)
		Rotator->homeInterrupt();
}

void IRAM_ATTR conditionsIntHandler()
{
	if(Rotator)
		Rotator->conditionsInterrupt();
}

void IRAM_ATTR buttonEastHandler()
{
	if(Rotator)
		Rotator->ButtonEastCheck();
}

void IRAM_ATTR buttonWestHandler()
{
	if(Rotator)
		Rotator->ButtonWestCheck();
}

void IRAM_ATTR openShutterButtonInt()
{
	bOpenShutterButtonPressed = true;
}

void IRAM_ATTR closeShutterButtonInt()
{
	bCloseShutterButtonPressed = true;

}

// reset chip with /reset connected to nPin
void resetChip(int nPin)
{
	digitalWrite(nPin, 0);
	vTaskDelay(2 / portTICK_PERIOD_MS);
	digitalWrite(nPin, 1);
	vTaskDelay(10 / portTICK_PERIOD_MS);
}

#ifdef USE_WIFI

void SendHello()
{
	String shutterMessage;

	if(nbWiFiClient && shutterClient.connected()) {
		DBPrintln("Sending hello");
		shutterMessage = String(HELLO) + "#";
		shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
		vTaskDelay(DELAY_WIFI / portTICK_PERIOD_MS);
		ReceiveWiFi(shutterClient);
		bSentHello = true;
	}
}

void requestWiFiShutterData()
{
	String shutterMessage;
	if(nbWiFiClient && shutterClient.connected()) {
		shutterMessage = String(STATE_SHUTTER) + "#";
		shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
		vTaskDelay(DELAY_WIFI / portTICK_PERIOD_MS);
		ReceiveWiFi(shutterClient);

		shutterMessage = String(VERSION_SHUTTER) + "#";
		shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
		vTaskDelay(DELAY_WIFI / portTICK_PERIOD_MS);
		ReceiveWiFi(shutterClient);

		shutterMessage = String(REVERSED_SHUTTER) + "#";
		shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
		vTaskDelay(DELAY_WIFI / portTICK_PERIOD_MS);
		ReceiveWiFi(shutterClient);

		shutterMessage = String(STEPSPER_SHUTTER) + "#";
		shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
		vTaskDelay(DELAY_WIFI / portTICK_PERIOD_MS);
		ReceiveWiFi(shutterClient);

		shutterMessage = String(SPEED_SHUTTER) + "#";
		shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
		vTaskDelay(DELAY_WIFI / portTICK_PERIOD_MS);
		ReceiveWiFi(shutterClient);

		shutterMessage = String(ACCELERATION_SHUTTER) + "#";
		shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
		vTaskDelay(DELAY_WIFI / portTICK_PERIOD_MS);
		ReceiveWiFi(shutterClient);

		shutterMessage = String(VOLTS_SHUTTER) + "#";
		shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
		vTaskDelay(DELAY_WIFI / portTICK_PERIOD_MS);
		ReceiveWiFi(shutterClient);

		shutterMessage = String(DOUBLE_SHUTTER) + "#";
		shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
		vTaskDelay(DELAY_WIFI / portTICK_PERIOD_MS);
		ReceiveWiFi(shutterClient);

		shutterMessage = String(SHUTTER_ORDER) + "#";
		shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
		vTaskDelay(DELAY_WIFI / portTICK_PERIOD_MS);
		ReceiveWiFi(shutterClient);
	}
}
#endif

void CheckForCommands()
{
	ReceiveComputer();

	if(ethernetPresent ) {
		ReceiveNetwork(domeClient);
	}
#ifdef USE_WIFI
	ReceiveWiFi(shutterClient);
#endif // USE_WIFI
}

void CheckForConditions()
{
	String shutterMessage;
	bool bCurrentCondition = Rotator->GetConditionsStatus();

	// float nPosition;
	float dParkPos;
	if(bIsSafe != bCurrentCondition) { // was there a state change ?
		bIsSafe = bCurrentCondition;
#ifdef USE_WIFI
		if(nbWiFiClient && shutterClient.connected()) {
			shutterMessage = String(CONDITION_SHUTTER) + String(bIsSafe ? String(COND_SAFE) : String(UNSAFE)) + "#";
			shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
			vTaskDelay(DELAY_WIFI / portTICK_PERIOD_MS);
			ReceiveWiFi(shutterClient);
		}
#endif // USE_WIFI
	}
	if (!bIsSafe) {
		if (Rotator->GetConditionsAction() == HOME  && Rotator->GetHomeStatus() != ATHOME  && Rotator->GetSeekMode() == NOT_MOVING) {			
			DBPrintln("Bad Conditions- > Homing");
#ifdef MOTION_LOG
			logMotion("Conditions:Home", Rotator->GetHomeAzimuth());
#endif
			Rotator->StartHoming();
		}

		if (Rotator->GetConditionsAction() == PARK && !bGlobalParked) {
			dParkPos = Rotator->GetParkAzimuth();
#ifdef MOTION_LOG
			logMotion("Conditions:Park", dParkPos);
#endif
			DBPrintln("Bad Conditions -> Parking");
			Rotator->GoToAzimuth(dParkPos);
			bGlobalParked = true;
		}
	// keep telling the shutter about the Conditions status
#ifdef USE_WIFI
		if(nbWiFiClient && shutterClient.connected()) {
			shutterMessage = String(CONDITION_SHUTTER) + String(bIsSafe ? String(COND_SAFE) : String(UNSAFE)) + "#";
			shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
		}
#endif // USE_WIFI
	}
}

#ifdef USE_WIFI

void checkShuterLowVoltage()
{

	bLowShutterVoltage = (RemoteShutter.lowVoltState.equals("L"));
	if(bLowShutterVoltage && !bGlobalParked) {
#ifdef MOTION_LOG
		logMotion(__func__, Rotator->GetParkAzimuth());
#endif
		 Rotator->GoToAzimuth(Rotator->GetParkAzimuth()); // we need to park so we can recharge the shutter battery
		 bGlobalParked = true;
	}
}

void PingWiFiShutter()
{
	String shutterMessage;
	int nTime = 0;
	if(PingTimer.elapsed() >= pingInterval) {
		if(nbWiFiClient && shutterClient.connected()) {
			DBPrintln("PingWiFiShutter");
			shutterMessage = String(SHUTTER_PING) + "#";
			shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
			vTaskDelay(DELAY_WIFI / portTICK_PERIOD_MS);
			ReceiveWiFi(shutterClient);
			PingTimer.reset();
			//ask for shutter voltages
			shutterMessage = String(VOLTS_SHUTTER) + "#";
			shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
			vTaskDelay(DELAY_WIFI / portTICK_PERIOD_MS);
			ReceiveWiFi(shutterClient);
			// get current shutter state
			shutterMessage = String(STATE_SHUTTER) + "#";
			shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
			vTaskDelay(DELAY_WIFI / portTICK_PERIOD_MS);
			ReceiveWiFi(shutterClient);
		}
		else if(nbWiFiClient && !shutterClient.connected()) {
			DBPrintln("shutterClient is gone");
			shutterClient.stop();
			nbWiFiClient--;
		}
	}
}
#endif

void ReceiveNetwork(NetworkClient client)
{
	char networkCharacter;

	if(!client.connected()) {
		return;
	}

	if(client.available() < 1)
		return; // no data

	while(client.available()>0) {
		networkCharacter = client.read();
		if (networkCharacter != ERR_NO_DATA) {
			if (networkCharacter == '\r' || networkCharacter == '\n' || networkCharacter == '#') {
				// End of message
				if (networkBuffer.length() > 0) {
					ProcessCommand(NETWORK_CMD);
					networkBuffer = "";
					return; // we'll read the next command on the next loop.
				}
			}
			else {
				networkBuffer += String(networkCharacter);
			}
		}
	}
}

#ifdef USE_WIFI
void ReceiveWiFi(WiFiClient client)
{
	char networkCharacter;

	if(!client.connected()) {
		return;
	}

	if(client.available() < 1)
		return; // no data

	while(client.available()>0) {
		networkCharacter = client.read();
		if (networkCharacter != ERR_NO_DATA) {
			if (networkCharacter == '\r' || networkCharacter == '\n' || networkCharacter == '#') {
				// End of message
				if (wifiBuffer.length() > 0) {
					ProcessWifi();
					wifiBuffer = "";
					return; // we'll read the next command on the next loop.
				}
			}
			else {
				wifiBuffer += String(networkCharacter);
			}
		}
	}
}
#endif

// All comms are terminated with '#' but the '\r' and '\n' are for debugging
void ReceiveComputer()
{
	char computerCharacter;

	if(!Computer)
		return;

	if(Computer.available() < 1)
		return; // no data

	while(Computer.available() > 0 ) {
		computerCharacter = Computer.read();
		if (computerCharacter != ERR_NO_DATA) {
			if (computerCharacter == '\r' || computerCharacter == '\n' || computerCharacter == '#') {
				// End of message
				if (computerBuffer.length() > 0) {
					ProcessCommand(SERIAL_CMD);
					computerBuffer = "";
					return; // we'll read the next command on the next loop.
				}
			}
			else {
				computerBuffer += String(computerCharacter);
			}
		}
	}
}

void ProcessCommand(int nSource)
{
	float fTmp;
	char command;
	String value;

#ifdef USE_WIFI
	String shutterMessage;
#endif // USE_WIFI
	String serialMessage, sTmpString;
	bool hasValue = false;

	// Split the buffer into command char and value if present
	// Command character
	switch(nSource) {
		case SERIAL_CMD:
			command = computerBuffer.charAt(0);
			// Payload
			value = computerBuffer.substring(1);
			break;
		case NETWORK_CMD:
			command = networkBuffer.charAt(0);
			// Payload
			value = networkBuffer.substring(1);
			break;
	}

	// payload has data
	if (value.length() > 0)
		hasValue = true;

	serialMessage = "";
#ifdef USE_WIFI
	shutterMessage = "";
#endif // USE_WIFI

	DBPrintln("\nProcessCommand");
	DBPrintln("Command = \"" + String(command) +"\"");
	DBPrintln("Value = \"" + String(value) +"\"");
	DBPrintln("nSource = " + String(nSource));


	switch (command) {
		case ABORT:
			sTmpString = String(ABORT);
			serialMessage = sTmpString;
			Abort();
			break;

		case ACCELERATION_ROTATOR:
			if (hasValue) {
				Rotator->SetAcceleration(value.toInt());
			}
			serialMessage = String(ACCELERATION_ROTATOR) + String(Rotator->GetAcceleration());
			break;

		case CALIBRATE_ROTATOR:
			Rotator->StartCalibrating();
			serialMessage = String(CALIBRATE_ROTATOR);
			break;

		case GOTO_ROTATOR:
			if (hasValue && !bLowShutterVoltage) { // stay at park if shutter voltage is low.
				fTmp = value.toFloat();
				while(fTmp < 0.0f) {
					fTmp += 360.0f;
				}
				while(fTmp > 360.0f) {
					fTmp -= 360.0f;
				}
				Rotator->GoToAzimuth(fTmp);
				bGlobalParked = false;
			}
			DBPrintln("Azimuth : " + String(Rotator->GetAzimuth()));
			serialMessage = String(GOTO_ROTATOR) + String(Rotator->GetAzimuth());
			break;
#ifdef USE_WIFI
		case HELLO:
			SendHello();
			serialMessage = String(HELLO);
			break;
#endif // USE_WIFI
		case HOME_ROTATOR:
			Rotator->StartHoming();
			serialMessage = String(HOME_ROTATOR);
			break;

		case HOMEAZ_ROTATOR:
			if (hasValue) {
				fTmp = value.toDouble();
				if ((fTmp >= 0.0f) && (fTmp < 360.0f))
					Rotator->SetHomeAzimuth(fTmp);
			}
			serialMessage = String(HOMEAZ_ROTATOR) + String(Rotator->GetHomeAzimuth());
			break;

		case HOMESTATUS_ROTATOR:
			serialMessage = String(HOMESTATUS_ROTATOR) + String(Rotator->GetHomeStatus());
			break;

		case PARKAZ_ROTATOR:
			sTmpString = String(PARKAZ_ROTATOR);
			if (hasValue) {
				fTmp = value.toDouble();
				if ((fTmp >= 0.0f) && (fTmp < 360.0f)) {
					Rotator->SetParkAzimuth(fTmp);
					serialMessage = sTmpString + String(Rotator->GetParkAzimuth());
				}
				else {
					serialMessage = sTmpString + "E";
				}
			}
			else {
				serialMessage = sTmpString + String(Rotator->GetParkAzimuth());
			}
			break;

		case CONDITION_ROTATOR_ACTION:
			if (hasValue) {
				Rotator->SetConditionsAction(value.toInt());
			}
			serialMessage = String(CONDITION_ROTATOR_ACTION) + String(Rotator->GetConditionsAction());
			break;

		case SPEED_ROTATOR:
			if (hasValue)
				Rotator->SetMaxSpeed(value.toInt());
			serialMessage = String(SPEED_ROTATOR) + String(Rotator->GetMaxSpeed());
			break;

		case REVERSED_ROTATOR:
			if (hasValue)
				Rotator->SetReversed(value.toInt()==0?false:true);
			serialMessage = String(REVERSED_ROTATOR) + String(Rotator->GetReversed()?"1":"0");
			break;

		case RESTORE_MOTOR_DEFAULT:
			Rotator->restoreDefaultMotorSettings();
			serialMessage = String(RESTORE_MOTOR_DEFAULT);
			break;

		case SLEW_ROTATOR:
			serialMessage = String(SLEW_ROTATOR) + String(Rotator->GetDirection());
			break;

		case STEPSPER_ROTATOR:
			if (hasValue)
				Rotator->SetStepsPerRotation(value.toInt());
			serialMessage = String(STEPSPER_ROTATOR) + String(Rotator->GetStepsPerRotation());
			break;

		case SYNC_ROTATOR:
			if (hasValue) {
				fTmp = value.toDouble();
				if (fTmp >= 0.0f && fTmp < 360.0f) {
					Rotator->SyncPosition(fTmp);
					serialMessage = String(SYNC_ROTATOR) + String(Rotator->GetAzimuth());
				}
			}
			else {
					serialMessage = String(SYNC_ROTATOR) + "E";
			}
			break;

		case VERSION_ROTATOR:
			serialMessage = String(VERSION_ROTATOR) + VERSION;
			break;

		case VOLTS_ROTATOR:
			serialMessage = String(VOLTS_ROTATOR) + String("1200,1000");
			break;

		case CONDITION_SHUTTER:
			serialMessage = String(CONDITION_SHUTTER) + String(bIsSafe ? String(COND_SAFE) : String(UNSAFE));
			break;

		case IS_SHUTTER_PRESENT:
			serialMessage = String(IS_SHUTTER_PRESENT) + String( bShutterPresent? "1" : "0");
			break;

		case ETH_RECONFIG :
			if(nbNetworkClient > 0) {
				domeClient.stop();
				nbNetworkClient--;
			}
			DBPrintln("Rebooting for Ethernet reconfiguration");
			vTaskDelay(500 / portTICK_PERIOD_MS);
			ESP.restart();
			break;

		case ETH_MAC_ADDRESS:
			char macBuffer[20];
			snprintf(macBuffer,20,"%02x:%02x:%02x:%02x:%02x:%02x",
					MAC_Address[0],
					MAC_Address[1],
					MAC_Address[2],
					MAC_Address[3],
					MAC_Address[4],
					MAC_Address[5]);

			serialMessage = String(ETH_MAC_ADDRESS) + String(macBuffer);
			break;

		case IP_DHCP:
			if (hasValue) {
				Rotator->setDHCPFlag(value.toInt() == 0 ? false : true);
			}
			serialMessage = String(IP_DHCP) + String( Rotator->getDHCPFlag()? "1" : "0");
			break;

		case IP_ADDRESS:
			if (hasValue) {
				Rotator->setIPAddress(value);
				Rotator->getIpConfig(ServerConfig);
			}
			if(!ServerConfig.bUseDHCP)
				serialMessage = String(IP_ADDRESS) + String(Rotator->getIPAddress());
			else {
				serialMessage = String(IP_ADDRESS) + String(RotatorClass::IpAddress2String(domeEthernet.localIP()));
			}
			break;

		case IP_SUBNET:
			if (hasValue) {
				Rotator->setIPSubnetMask(value);
				Rotator->getIpConfig(ServerConfig);
			}
			if(!ServerConfig.bUseDHCP)
				serialMessage = String(IP_SUBNET) + String(Rotator->getIPSubnetMask());
			else {
				serialMessage = String(IP_SUBNET) + String(RotatorClass::IpAddress2String(domeEthernet.subnetMask()));
			}
			break;

		case IP_GATEWAY:
			if (hasValue) {
				Rotator->setIPGateway(value);
				Rotator->getIpConfig(ServerConfig);
			}
			if(!ServerConfig.bUseDHCP)
				serialMessage = String(IP_GATEWAY) + String(Rotator->getIPGateway());
			else {
				serialMessage = String(IP_GATEWAY) + String(RotatorClass::IpAddress2String(domeEthernet.gatewayIP()));
			}
			break;

		case RESTORE_NET_DEFAULT:
			Rotator->resetNetworkToDefaults();
			serialMessage = String(RESTORE_NET_DEFAULT);
			break;

		case RESET_ALL:
			shutterMessage = String(RESET_ALL) + "#";
			shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
			vTaskDelay(250 / portTICK_PERIOD_MS);
			Rotator->resetAlltoDefault(); // this reboots the ESP.
			break;

#ifdef USE_WIFI
		case DOUBLE_SHUTTER:
			sTmpString = String(DOUBLE_SHUTTER);
			if (hasValue) {
				RemoteShutter.bDualShutterEnabled = (value.toInt()==0?false:true);
				shutterMessage = sTmpString + value;
			}
			else {
				shutterMessage = sTmpString;
			}
			if(nbWiFiClient && shutterClient.connected()) {
				shutterMessage += "#";
				shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
				vTaskDelay(DELAY_WIFI / portTICK_PERIOD_MS);
				ReceiveWiFi(shutterClient);
			}
			serialMessage = sTmpString + (RemoteShutter.bDualShutterEnabled?"1":"0");
			break;

		case SHUTTER_ORDER:
			sTmpString = String(SHUTTER_ORDER);
			if (hasValue) {
				RemoteShutter.nShutterOrder = value.toInt();
				shutterMessage = sTmpString + value;
			}
			else {
				shutterMessage = sTmpString;
			}
			if(nbWiFiClient && shutterClient.connected()) {
				shutterMessage += "#";
				shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
				vTaskDelay(DELAY_WIFI / portTICK_PERIOD_MS);
				ReceiveWiFi(shutterClient);
			}
			serialMessage = sTmpString + String(RemoteShutter.nShutterOrder);
			break;

		case SSID:
			if (hasValue) {
				Rotator->setSSID(value);
				// send new SSID to shutter
				shutterMessage = String(SHUTTER_SSID) + value + "#";
				shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
				vTaskDelay(250 / portTICK_PERIOD_MS);
				// reconfigure wifi
				configureWiFi();
			}
			serialMessage = String(SSID) + Rotator->getSSID();
			break;

		case SHUTTER_SSID:
			if(nbWiFiClient && shutterClient.connected()) {
				shutterClient.write("Q#",2);
				vTaskDelay(DELAY_WIFI / portTICK_PERIOD_MS);
				ReceiveWiFi(shutterClient);
			}
			serialMessage = String(SHUTTER_SSID) + RemoteShutter.ssid;
			break;

		case SHUTTER_PING:
			shutterMessage = String(SHUTTER_PING);
			if(nbWiFiClient && shutterClient.connected()) {
				shutterMessage += "#";
				shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
				vTaskDelay(DELAY_WIFI / portTICK_PERIOD_MS);
				ReceiveWiFi(shutterClient);
			}
			serialMessage = String(SHUTTER_PING);
			break;

		case ACCELERATION_SHUTTER:
			sTmpString = String(ACCELERATION_SHUTTER);
			if (hasValue) {
				RemoteShutter.acceleration = value.toInt();
				shutterMessage = sTmpString + value;
			}
			else {
				shutterMessage = sTmpString;
			}
			if(nbWiFiClient && shutterClient.connected()) {
				shutterMessage += "#";
				shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
				vTaskDelay(DELAY_WIFI / portTICK_PERIOD_MS);
				ReceiveWiFi(shutterClient);
			}
			serialMessage = sTmpString + String(RemoteShutter.acceleration);
			break;

		case CLOSE_SHUTTER:
			sTmpString = String(CLOSE_SHUTTER);
			if(nbWiFiClient && shutterClient.connected()) {
				shutterMessage = sTmpString+ "#";
				shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
				vTaskDelay(DELAY_WIFI / portTICK_PERIOD_MS);
				ReceiveWiFi(shutterClient);
			}
			serialMessage = sTmpString;
			break;

		case SHUTTER_RESTORE_MOTOR_DEFAULT :
			sTmpString = String(SHUTTER_RESTORE_MOTOR_DEFAULT);
			if(nbWiFiClient && shutterClient.connected()) {
				shutterMessage = sTmpString+ "#";
				shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
				vTaskDelay(DELAY_WIFI / portTICK_PERIOD_MS);
				ReceiveWiFi(shutterClient);
				shutterMessage = String(SPEED_SHUTTER)+ "#";
				shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
				vTaskDelay(DELAY_WIFI / portTICK_PERIOD_MS);
				ReceiveWiFi(shutterClient);
				shutterMessage = String(ACCELERATION_SHUTTER)+ "#";
				shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
				vTaskDelay(DELAY_WIFI / portTICK_PERIOD_MS);
				ReceiveWiFi(shutterClient);
			}
			serialMessage = sTmpString;
			break;

//      case ELEVATION_SHUTTER:
//          sTmpString = String(ELEVATION_SHUTTER);
//          if (hasValue) {
//              RemoteShutter.position = value;
//              wirelessMessage = sTmpString + RemoteShutter.position;
//          }
//          else {
//              wirelessMessage = sTmpString;
//          }
//			shutterMessage += "#";
//			shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
//			vTaskDelay(DELAY_WIFI / portTICK_PERIOD_MS);
//          ReceiveWiFi(shutterClient);
//          serialMessage = sTmpString + RemoteShutter.position;
//          break;

		case OPEN_SHUTTER:
			sTmpString = String(OPEN_SHUTTER);
			if(nbWiFiClient && shutterClient.connected()) {
				shutterMessage = sTmpString+ "#";
				shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
				vTaskDelay(DELAY_WIFI / portTICK_PERIOD_MS);
				ReceiveWiFi(shutterClient);
				}
			serialMessage = sTmpString + RemoteShutter.lowVoltState;
			break;

		case REVERSED_SHUTTER:
			sTmpString = String(REVERSED_SHUTTER);
			if (hasValue) {
				RemoteShutter.reversed = value;
				shutterMessage = sTmpString + value;
			}
			else {
				shutterMessage = sTmpString;
			}
			if(nbWiFiClient && shutterClient.connected()) {
				shutterMessage += "#";
				shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
				vTaskDelay(DELAY_WIFI / portTICK_PERIOD_MS);
				ReceiveWiFi(shutterClient);
			}
			serialMessage = sTmpString + RemoteShutter.reversed;
			break;

		case SPEED_SHUTTER:
			sTmpString = String(SPEED_SHUTTER);
			if (hasValue) {
				RemoteShutter.speed = value.toInt();
				shutterMessage = sTmpString + String(RemoteShutter.speed);
			}
			else {
				shutterMessage = sTmpString;
			}
			if(nbWiFiClient && shutterClient.connected()) {
				shutterMessage += "#";
				shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
				vTaskDelay(DELAY_WIFI / portTICK_PERIOD_MS);
				ReceiveWiFi(shutterClient);
			}
			serialMessage = sTmpString + RemoteShutter.speed;
			break;

		case STATE_SHUTTER:
			sTmpString = String(STATE_SHUTTER);
			if(nbWiFiClient && shutterClient.connected()) {
				shutterMessage = sTmpString+ "#";
				shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
				vTaskDelay(DELAY_WIFI / portTICK_PERIOD_MS);
				ReceiveWiFi(shutterClient);
			}
			serialMessage = sTmpString + RemoteShutter.state;
			break;

		case STEPSPER_SHUTTER:
			sTmpString = String(STEPSPER_SHUTTER);
			if (hasValue) {
				RemoteShutter.stepsPerStroke = value.toInt();
				shutterMessage = sTmpString + value;
			}
			else {
				shutterMessage = sTmpString;
			}
			if(nbWiFiClient && shutterClient.connected()) {
				shutterMessage += "#";
				shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
				vTaskDelay(DELAY_WIFI / portTICK_PERIOD_MS);
				ReceiveWiFi(shutterClient);
			}
			serialMessage = sTmpString + String(RemoteShutter.stepsPerStroke);
			break;

		case VERSION_SHUTTER:
			sTmpString = String(VERSION_SHUTTER);
			if(nbWiFiClient && shutterClient.connected()) {
				shutterMessage += "#";
				shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
				vTaskDelay(DELAY_WIFI / portTICK_PERIOD_MS);
				ReceiveWiFi(shutterClient);
			}
			serialMessage = sTmpString + RemoteShutter.version;
			break;

		case VOLTS_SHUTTER:
			sTmpString = String(VOLTS_SHUTTER);
			shutterMessage = sTmpString;
			if (hasValue) {
				shutterMessage += value;
				RemoteShutter.voltsCutOff = value.toInt();
			}
			if(nbWiFiClient && shutterClient.connected()) {
				shutterMessage += "#";
				shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
				vTaskDelay(DELAY_WIFI / portTICK_PERIOD_MS);
				ReceiveWiFi(shutterClient);
			}
			serialMessage = sTmpString +  String(RemoteShutter.volts) + "," + String(RemoteShutter.voltsCutOff);
			break;

		case WATCHDOG_INTERVAL:
			sTmpString = String(WATCHDOG_INTERVAL);
			if (value.length() > 0) {
				shutterMessage = sTmpString + value;
			}
			else {
				shutterMessage = sTmpString;
			}
			if(nbWiFiClient && shutterClient.connected()) {
				shutterMessage += "#";
				shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
				vTaskDelay(DELAY_WIFI / portTICK_PERIOD_MS);
				ReceiveWiFi(shutterClient);
			}
			serialMessage = sTmpString + RemoteShutter.watchdogInterval;
			break;
#endif // USE_WIFI

		default:
			serialMessage = "Unknown command:" + String(command);
			break;
	}


	// Send messages if they aren't empty.
	if (serialMessage.length() > 0) {
		serialMessage += "#";
		switch(nSource) {
			case SERIAL_CMD:
				if(Computer) {
					DBPrintln("Computer serialMessage = " + serialMessage);
					Computer.write(serialMessage.c_str(), serialMessage.length());
					Computer.flush();
				}
				break;
			case NETWORK_CMD:
				if(domeClient.connected()) {
					DBPrintln("Network serialMessage = " + serialMessage);
					domeClient.write(serialMessage.c_str(), serialMessage.length());
				}
				break;
		}
	}
}

#ifdef USE_WIFI
void ProcessWifi()
{
	char command;
	bool hasValue = false;
	String value;
	String shutterMessage;

	DBPrintln("<<< Received: '" + wifiBuffer + "'");
	command = wifiBuffer.charAt(0);
	value = wifiBuffer.substring(1);
	if (value.length() > 0)
		hasValue = true;

	// we got data so the shutter is alive
	ShutterWatchdog.reset();
	bShutterPresent = true;

	switch (command) {
		case ACCELERATION_SHUTTER:
			if (hasValue)
				RemoteShutter.acceleration = value.toInt();
			break;

		case HELLO:
			bGotHelloFromShutter = true;
			bShutterPresent = true;
			break;

		case SPEED_SHUTTER:
			if (hasValue)
				RemoteShutter.speed = value.toInt();
			break;

		case CONDITION_SHUTTER:
			if(nbWiFiClient && shutterClient.connected()) {
				shutterMessage = String(CONDITION_SHUTTER) + String(bIsSafe ? String(COND_SAFE): String(UNSAFE)) + "#";
				shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
			}
			break;

		case REVERSED_SHUTTER:
			if (hasValue)
				RemoteShutter.reversed = value;
			break;

		case STATE_SHUTTER:
			if (hasValue)
				RemoteShutter.state = value.toInt();
			break;

		case OPEN_SHUTTER:
			if (hasValue)
				RemoteShutter.lowVoltState = value;
			else
				RemoteShutter.lowVoltState = "";
			break;

		case STEPSPER_SHUTTER:
			if (hasValue)
				RemoteShutter.stepsPerStroke = value.toInt();
			break;

		case VERSION_SHUTTER:
			if (hasValue)
				RemoteShutter.version = value;
			break;

		case VOLTS_SHUTTER:
			if (hasValue) {
				String sVolts = value.substring(0,value.indexOf(","));
				String sVoltsCutOff = value.substring(value.indexOf(",")+1);
				RemoteShutter.volts = sVolts.toInt();
				RemoteShutter.voltsCutOff = sVoltsCutOff.toInt();
#ifdef MOTION_LOG
				if(RemoteShutter.volts < RemoteShutter.voltsCutOff) {
					logMotion("VOLTS_SHUTTER Low voltage", RemoteShutter.volts);
				}
#endif
			}
			break;


		case WATCHDOG_INTERVAL:
			if (hasValue)
				RemoteShutter.watchdogInterval = value.toInt();
			break;

		case SHUTTER_PING:
			bShutterPresent = true;
			if (hasValue) {
				RemoteShutter.lowVoltState = value;
#ifdef MOTION_LOG
				if(value.equals("L"))
					logMotion("PING set lowVoltState=L", RemoteShutter.volts);
#endif
			}
			else
				RemoteShutter.lowVoltState = "";

			break;

		 case SHUTTER_RESTORE_MOTOR_DEFAULT:
			break;

		case SHUTTER_SSID:
			if (hasValue)
				 RemoteShutter.ssid = value;
			break;

		case DOUBLE_SHUTTER:
			if (hasValue)
				RemoteShutter.bDualShutterEnabled = bool(value.toInt()==0?false:true);
			break;
		
		case SHUTTER_ORDER:
			if (hasValue)
				RemoteShutter.nShutterOrder = value.toInt();
			break;

		default:
			break;
	}

}
#endif

void Abort()
{
	String shutterMessage;
	if(Rotator)
			Rotator->Stop();
#ifdef USE_WIFI
	if(nbWiFiClient && shutterClient.connected()) {
		shutterMessage = String(ABORT) + "#";
		shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
		vTaskDelay(DELAY_WIFI / portTICK_PERIOD_MS);
		ReceiveWiFi(shutterClient);
	}
#endif
}
