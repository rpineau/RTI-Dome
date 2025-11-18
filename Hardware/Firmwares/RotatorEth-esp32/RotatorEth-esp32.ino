//
// RTI-Zone Dome Rotator firmware.
// for ESP32
//
//  Copyright © 2024 Rodolphe Pineau. All rights reserved.
//


// Uncomment #define DEBUG to enable printing debug messages on serial port defined as DebugPort

#include "Arduino.h"
#include <rtc_wdt.h>
#include <esp_task_wdt.h>
#include <atomic>
// #define DEBUG   // enable debug to serial port defined as DebugPort

#ifdef DEBUG
#pragma message "Debug messages enabled"
#define DebugPort Serial1    //  Rx2,Tx2 =  Serial1
#define DBPrint(x) if(DebugPort) DebugPort.print(x)
#define DBPrintln(x) if(DebugPort) DebugPort.println(x)
#define DBPrintHex(x) if(DebugPort) DebugPort.print(x, HEX)
#else
#pragma message "Debug messages disabled"
#define DBPrint(x)
#define DBPrintln(x)
#define DBPrintHex(x)
#endif // DEBUG

#define VERSION "2.645"
#define MAX_TIMEOUT 10

#define USE_EXT_EEPROM
#define USE_ETHERNET
#define USE_ALPACA
// if uncommented, USE_WIFI will enable all code related to the shutter over WiFi.
// This is useful for people who only want to automate the rotation.
#define USE_WIFI

#define Computer Serial     // USB = Serial

#include "RotatorClass.h"

#ifdef USE_ETHERNET
#pragma message "Ethernet enabled"
// include and some defines for ethernet connection
#include <SPI.h>    // ESP32 :  SCK: GPIO18, SDO/TX: GPIO23, SDI: GPIO19, CS: GPIO5, Reset : GPIO29, Int : GPIO0
#include <Ethernet.h>
#include "EtherMac.h"
#define ETHERNET_CS     5
#define ETHERNET_INT	0
#define ETHERNET_RESET  4
#define CMD_SERVER_PORT 2323
#define domeEthernet Ethernet
uint32_t uidBuffer[4];  // Board unique ID
byte MAC_Address[6];    // Mac address, uses part of the unique ID
IPConfig ServerConfig;
std::atomic<bool> ethernetPresent{false};
EthernetServer *domeServer = nullptr;
EthernetClient domeClient;
int nbEthernetClient = 0;
String networkBuffer = "";
String sLocalIPAdress = "";
#endif // USE_ETHERNET

#ifdef USE_WIFI
#pragma message "Local WiFi shutter enable"
#include "RemoteShutterClass.h"
#include <WiFi.h>
#include <WiFiAP.h>
#define SHUTTER_PORT 2424
#define shutterWiFi WiFi
std::atomic<bool> wifiPresent{false};
WIFIConfig wifiConfig;
WiFiServer *shutterServer = nullptr;
WiFiClient shutterClient;
String wifiBuffer = "";
int nbWiFiClient = 0;
String sLocalWifiIPAddress;
std::atomic<bool> bGotHelloFromShutter{false};
RemoteShutterClass RemoteShutter;
#endif

String computerBuffer = "";

bool bParked = false; // use to the run check doesn't continuously try to park

RotatorClass *Rotator = NULL;

static const unsigned long pingInterval = 5000; // 5 seconds, can't be changed with command

// Once booting is done and XBee is ready, broadcast a hello message
// so a shutter knows you're around if it is already running. If not,
// the shutter will send a hello when it boots.
std::atomic<bool> bSentHello{false};

#ifdef USE_WIFI
// Timer to periodically ping the shutter
StopWatch PingTimer;
StopWatch ShutterWatchdog;
#endif

std::atomic<bool> bShutterPresent{false};
// global variable for condition status
std::atomic<bool> bIsSafe{true};
// global variable for shutter voltage state
std::atomic<bool> bLowShutterVoltage{false};

const char ERR_NO_DATA = -1;

#include "dome_commands.h"
enum CmdSource {SERIAL_CMD, NETWORK_CMD};
// function prototypes
#ifdef USE_ETHERNET
void configureEthernet();
bool initEthernet(bool bUseDHCP, IPAddress ip, IPAddress dns, IPAddress gateway, IPAddress subnet, bool bReconfigure);
void checkForNewTCPClient();
#endif // USE_ETHERNET
#ifdef USE_WIFI
void configureWiFi();
bool initWiFi(IPAddress ip, String sSSID, String sPassword);
void checkForNewWifiClient();
#endif
void homeIntHandler();
void conditionsIntHandler();
void buttonHandler();
void resetChip(int);
void StartWirelessConfig();
void ConfigXBee();
void SendHello();
void requestShutterData();
void CheckForCommands();
void CheckForConditions();
#ifdef USE_ETHERNET
void ReceiveNetwork(EthernetClient client);
#endif // USE_ETHERNET
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
esp_task_wdt_config_t twdt_config =
    {
        .timeout_ms = 1000000,
        .idle_core_mask = 0,    // Bitmask of cores
        .trigger_panic = false,
    };

//
// Setup and main loops
//
void setup()
{
#ifdef USE_WIFI
	nbWiFiClient = 0;
#endif
	nbEthernetClient = 0;

#ifdef DEBUG
	DebugPort.begin(115200, SERIAL_8N1, 16, 17); // pins 16 rx2, 17 tx2, 115200 bps, 8 bits no parity 1 stop bit
	//DebugPort.begin(115200);
	delay(1000);
	DBPrintln("========== RTI-Zone controller booting ==========");
#endif

#ifdef USE_ETHERNET
	digitalWrite(ETHERNET_RESET, 0);
	pinMode(ETHERNET_RESET, OUTPUT);
#endif // USE_ETHERNET

#ifdef USE_ETHERNET
	getMacAddress(MAC_Address, uidBuffer);
	DBPrintln("MAC : " + String(MAC_Address[0], HEX) + String(":") +
					String(MAC_Address[1], HEX) + String(":") +
					String(MAC_Address[2], HEX) + String(":") +
					String(MAC_Address[3], HEX) + String(":") +
					String(MAC_Address[4], HEX) + String(":") +
					String(MAC_Address[5], HEX) );
#endif // USE_ETHERNET

	Computer.begin(115200);
	//Computer.begin(115200, SERIAL_8N1, 16, 17); // pins 16 rx2, 17 tx2, 115200 bps, 8 bits no parity 1 stop bit


	Rotator = new RotatorClass();
	Rotator->motorStop();
	Rotator->Stop();
	Rotator->EnableMotor(false);


#ifdef USE_WIFI
	bSentHello = false;
	bGotHelloFromShutter = false;
	configureWiFi();
#endif

#ifdef USE_ETHERNET
	configureEthernet();
#endif // USE_ETHERNET
	// rtc_wdt_protect_off();
	esp_task_wdt_deinit();
	esp_task_wdt_init(&twdt_config);
	esp_task_wdt_add(NULL);
	disableCore0WDT();
	disableCore1WDT();
	xTaskCreatePinnedToCore(MotorTask, "MotorTask", 10000, NULL, 16, NULL,  0);

	domeServer = new EthernetServer(CMD_SERVER_PORT);
	domeServer->begin();
#ifdef USE_ALPACA
	AlpacaDiscoveryServer = new DomeAlpacaDiscoveryServer();
	AlpacaDiscoveryServer->startServer();
	AlpacaServer = new DomeAlpacaServer();
	AlpacaServer->startServer();
#endif
	DBPrintln("========== Ready ==========");
}

//
// These tasks take care of all communications and commands
//


void loop()
{
#ifdef USE_ETHERNET
	if(ethernetPresent) {
		checkForNewTCPClient();
		AlpacaDiscoveryServer->checkForRequest();
		AlpacaServer->checkForRequest();
	}
#endif //USE_ETHERNET

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
		taskYIELD();
		esp_task_wdt_reset();
}

//
// This task does all the motor controls
//
void MotorTask(void *)
{
	DBPrintln("========== Motor task starting ==========");
	DBPrintln("========== Motor task Attaching interrupt handler ==========");
	attachInterrupt(digitalPinToInterrupt(HOME_PIN), homeIntHandler, FALLING);
	attachInterrupt(digitalPinToInterrupt(CONDITION_SENSOR_PIN), conditionsIntHandler, CHANGE);
	attachInterrupt(digitalPinToInterrupt(BUTTON_CW), buttonHandler, CHANGE);
	attachInterrupt(digitalPinToInterrupt(BUTTON_CCW), buttonHandler, CHANGE);
	esp_task_wdt_add(NULL);
	DBPrintln("========== Motor task ready ==========");

	for(;;) {
		Rotator->Run();
		taskYIELD();
		esp_task_wdt_reset();
	}
}

//
//
//
#ifdef USE_ETHERNET
void configureEthernet()
{
        DBPrintln("========== Configuring Ethernet ==========");
        Rotator->getIpConfig(ServerConfig);
        ethernetPresent =  initEthernet(ServerConfig.bUseDHCP,
										ServerConfig.ip,
										ServerConfig.dns,
										ServerConfig.gateway,
										ServerConfig.subnet);
}


bool initEthernet(bool bUseDHCP, IPAddress ip, IPAddress dns, IPAddress gateway, IPAddress subnet)
{
	bool bDhcpOk;
	int nTimeout = 0;
	DBPrintln("========== Init Ethernet ==========");
	resetChip(ETHERNET_RESET);
	// network configuration
	Ethernet.init(ETHERNET_CS);
	nbEthernetClient = 0;
	// set an ip so we can get the link status
	domeEthernet.begin(MAC_Address, "192.168.0.1", "1.1.1.1", "192.168.0.254", "255.255.255.0");
	while(domeEthernet.linkStatus() == LinkOFF ) {
		delay(250);
		nTimeout++;
		if(nTimeout == 10) {
			return false;
		}
	}
	DBPrintln("========== Setting IP config ==========");
	// try DHCP if set
	if(bUseDHCP) {
		bDhcpOk = domeEthernet.begin(MAC_Address, 10000, 4000); // short timeout
		if(!bDhcpOk) {
			DBPrintln("DHCP Failed!");
			if(domeEthernet.linkStatus() == LinkON ) {
				domeEthernet.begin(MAC_Address, ip, dns, gateway, subnet);
			}
			else {
				DBPrintln("No cable");
				return false;
			}
		}
	}
	else {
		domeEthernet.begin(MAC_Address, ip, dns, gateway, subnet);
	}

	DBPrintln("========== Checking hardware status ==========");
	if(domeEthernet.hardwareStatus() == EthernetNoHardware) {
		 DBPrintln("NO HARDWARE !!!");
		return false;
	}
	DBPrintln("W5500 Ok.");
	DBPrintln("W5500 IP = " + RotatorClass::IpAddress2String(Ethernet.localIP()));
	Ethernet.setRetransmissionCount(3);

	DBPrintln("Server ready");
	return true;
}


void checkForNewTCPClient()
{
	if(ServerConfig.bUseDHCP)
		domeEthernet.maintain();

	if(!domeServer)
		return;

	EthernetClient newClient = domeServer->accept();
	if(newClient) {
		DBPrintln("new client");
		if(nbEthernetClient > 0) { // we only accept 1 client
			newClient.write("Already in use#");
			newClient.flush();
			newClient.stop();
			DBPrintln("new client rejected");
		}
		else {
			nbEthernetClient++;
			domeClient = newClient;
			DBPrintln("new client accepted");
			DBPrintln("nb client = " + String(nbEthernetClient));
		}
	}

	if((nbEthernetClient>0) && !domeClient.connected()) {
		DBPrintln("client disconnected");
		domeClient.stop();
		nbEthernetClient--;
		DBPrintln("nb client = " + String(nbEthernetClient));
	}
}
#endif // USE_ETHERNET

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

	wifiPresent = initWiFi(wifiConfig.ip,
								String(wifiConfig.sSSID),
								String(wifiConfig.sPassword));
}

bool initWiFi(IPAddress ip, String sSSID, String sPassword)
{
	bool bWiFiOk = true;

	shutterWiFi.disconnect();
	shutterWiFi.mode(WIFI_AP);
	bWiFiOk = shutterWiFi.softAP(sSSID.c_str(), sPassword.c_str());
	if(!bWiFiOk)
		return false;

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

void IRAM_ATTR buttonHandler()
{
   if(Rotator)
	   Rotator->ButtonCheck();
}


// reset chip with /reset connected to nPin
void resetChip(int nPin)
{
	digitalWrite(nPin, 0);
	delay(2);
	digitalWrite(nPin, 1);
	delay(10);
}

#ifdef USE_WIFI

void SendHello()
{
	String shutterMessage;

	if(nbWiFiClient && shutterClient.connected()) {
		DBPrintln("Sending hello");
		shutterMessage = String(HELLO) + "#";
		shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
		// shutterClient.flush();
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
		// shutterClient.flush();
		ReceiveWiFi(shutterClient);

		shutterMessage = String(VERSION_SHUTTER) + "#";
		shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
		// shutterClient.flush();
		ReceiveWiFi(shutterClient);

		shutterMessage = String(REVERSED_SHUTTER) + "#";
		shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
		// shutterClient.flush();
		ReceiveWiFi(shutterClient);

		shutterMessage = String(STEPSPER_SHUTTER) + "#";
		shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
		// shutterClient.flush();
		ReceiveWiFi(shutterClient);

		shutterMessage = String(SPEED_SHUTTER) + "#";
		shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
		// shutterClient.flush();
		ReceiveWiFi(shutterClient);

		shutterMessage = String(ACCELERATION_SHUTTER) + "#";
		shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
		// shutterClient.flush();
		ReceiveWiFi(shutterClient);

		shutterMessage = String(VOLTS_SHUTTER) + "#";
		shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
		// shutterClient.flush();
		ReceiveWiFi(shutterClient);
	}
}
#endif

void CheckForCommands()
{
	ReceiveComputer();

#ifdef USE_ETHERNET
	if(ethernetPresent ) {
		ReceiveNetwork(domeClient);
	}
#endif // USE_ETHERNET
#ifdef USE_WIFI
	if(wifiPresent) {
		ReceiveWiFi(shutterClient);
	}
#endif // USE_WIFI
}

void CheckForConditions()
{
	String shutterMessage;

	int nPosition, nParkPos;
	if(bIsSafe != Rotator->GetConditionsStatus()) { // was there a state change ?
		bIsSafe = Rotator->GetConditionsStatus();
#ifdef USE_WIFI
		if(nbWiFiClient && shutterClient.connected()) {
			shutterMessage = String(CONDITION_SHUTTER) + String(bIsSafe ? String(COND_SAFE) : String(UNSAFE)) + "#";
			shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
			// shutterClient.flush();
			ReceiveWiFi(shutterClient);
		}
#endif // USE_WIFI
	}
	if (!bIsSafe) {
		if (Rotator->GetConditionsAction() == HOME && Rotator->GetHomeStatus() != ATHOME) {
			DBPrintln("Bad Conditions- > Homing");
			Rotator->StartHoming();
		}

		if (Rotator->GetConditionsAction() == PARK && !bParked) {
			nParkPos = Rotator->GetParkAzimuth();
			DBPrintln("Bad Conditions -> Parking");
			Rotator->GoToAzimuth(nParkPos);
			bParked = true;
		}
	// keep telling the shutter about the Conditions status
#ifdef USE_WIFI
		if(nbWiFiClient && shutterClient.connected()) {
			shutterMessage = String(CONDITION_SHUTTER) + String(bIsSafe ? String(COND_SAFE) : String(UNSAFE)) + "#";
			shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
			// shutterClient.flush();
		}
#endif // USE_WIFI
	}
}

#ifdef USE_WIFI

void checkShuterLowVoltage()
{
	bLowShutterVoltage = (RemoteShutter.lowVoltStateOrBadConditions.equals("L"));
	if(bLowShutterVoltage) {
		 Rotator->GoToAzimuth(Rotator->GetParkAzimuth()); // we need to park so we can recharge the shutter battery
		 bParked = true;
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
			// shutterClient.flush();
			ReceiveWiFi(shutterClient);
			PingTimer.reset();
		}
		else if(nbWiFiClient && !shutterClient.connected()) {
			DBPrintln("shutterClient is gone");
			shutterClient.stop();
			nbWiFiClient--;
		}
	}
}
#endif

#ifdef USE_ETHERNET
void ReceiveNetwork(EthernetClient client)
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
#endif // USE_ETHERNET

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
#ifdef USE_ETHERNET
		case NETWORK_CMD:
			command = networkBuffer.charAt(0);
			// Payload
			value = networkBuffer.substring(1);
			break;
#endif
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
				bParked = false;
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
				Rotator->SetReversed(value.toInt());
			serialMessage = String(REVERSED_ROTATOR) + String(Rotator->GetReversed());
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
			if (hasValue) {
				Rotator->SetLowVoltageCutoff(value.toInt());
			}
			serialMessage = String(VOLTS_ROTATOR) + String(Rotator->GetVoltString());
			break;

		case CONDITION_SHUTTER:
			serialMessage = String(CONDITION_SHUTTER) + String(bIsSafe ? String(COND_SAFE) : String(UNSAFE));
			break;

		case IS_SHUTTER_PRESENT:
			serialMessage = String(IS_SHUTTER_PRESENT) + String( bShutterPresent? "1" : "0");
			break;
#ifdef USE_ETHERNET
		case ETH_RECONFIG :
			if(nbEthernetClient > 0) {
				domeClient.stop();
				nbEthernetClient--;
			}
			DBPrintln("Rebooting for Ethernet reconfiguration");
			delay(500);
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
				Rotator->setIPSubnet(value);
				Rotator->getIpConfig(ServerConfig);
			}
			if(!ServerConfig.bUseDHCP)
				serialMessage = String(IP_SUBNET) + String(Rotator->getIPSubnet());
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
#endif // USE_ETHERNET

#ifdef USE_WIFI
		case SSID:
			if (hasValue) {
				Rotator->setSSID(value);
				// send new SSID to shutter
				shutterMessage = String(SHUTTER_SSID) + value + "#";
				shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
				// reconfigure wifi
				configureWiFi();
			}
			serialMessage = String(SSID) + Rotator->getSSID();
			break;

		case SHUTTER_SSID:
			serialMessage = String(SHUTTER_SSID) + RemoteShutter.ssid;
			break;

		case SHUTTER_PING:
			shutterMessage = String(SHUTTER_PING);
			if(nbWiFiClient && shutterClient.connected()) {
				shutterMessage += "#";
				shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
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
				ReceiveWiFi(shutterClient);
			}
			serialMessage = sTmpString + String(RemoteShutter.acceleration);
			break;

		case CLOSE_SHUTTER:
			sTmpString = String(CLOSE_SHUTTER);
			if(nbWiFiClient && shutterClient.connected()) {
				shutterMessage = sTmpString+ "#";
				shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
				ReceiveWiFi(shutterClient);
			}
			serialMessage = sTmpString;
			break;

		case SHUTTER_RESTORE_MOTOR_DEFAULT :
			sTmpString = String(SHUTTER_RESTORE_MOTOR_DEFAULT);
			if(nbWiFiClient && shutterClient.connected()) {
				shutterMessage = sTmpString+ "#";
				shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
				ReceiveWiFi(shutterClient);
				shutterMessage = String(SPEED_SHUTTER)+ "#";
				shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
				ReceiveWiFi(shutterClient);
				shutterMessage = String(ACCELERATION_SHUTTER)+ "#";
				shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
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
//			// shutterClient.flush();
//          ReceiveWiFi(shutterClient);
//          serialMessage = sTmpString + RemoteShutter.position;
//          break;

		case OPEN_SHUTTER:
			sTmpString = String(OPEN_SHUTTER);
			if(nbWiFiClient && shutterClient.connected()) {
				shutterMessage = sTmpString+ "#";
				shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
				ReceiveWiFi(shutterClient);
				}
			serialMessage = sTmpString + RemoteShutter.lowVoltStateOrBadConditions;
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
				ReceiveWiFi(shutterClient);
			}
			serialMessage = sTmpString + RemoteShutter.speed;
			break;

		case STATE_SHUTTER:
			sTmpString = String(STATE_SHUTTER);
			if(nbWiFiClient && shutterClient.connected()) {
				shutterMessage = sTmpString+ "#";
				shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
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
				ReceiveWiFi(shutterClient);
			}
			serialMessage = sTmpString + String(RemoteShutter.stepsPerStroke);
			break;

		case VERSION_SHUTTER:
			sTmpString = String(VERSION_SHUTTER);
			if(nbWiFiClient && shutterClient.connected()) {
				shutterMessage += "#";
				shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
				ReceiveWiFi(shutterClient);
			}
			serialMessage = sTmpString + RemoteShutter.version;
			break;

		case VOLTS_SHUTTER:
			sTmpString = String(VOLTS_SHUTTER);
			shutterMessage = sTmpString;
			if (hasValue) {
				shutterMessage += value;
				RemoteShutter.voltsCutOff = value.toDouble();
			}
			if(nbWiFiClient && shutterClient.connected()) {
				shutterMessage += "#";
				shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
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
					Computer.write(serialMessage .c_str(), serialMessage.length());
				}
				break;
	#ifdef USE_ETHERNET
			case NETWORK_CMD:
				if(domeClient.connected()) {
					DBPrintln("Network serialMessage = " + serialMessage);
					domeClient.write(serialMessage .c_str(), serialMessage.length());
					domeClient.flush();
				}
				break;
	#endif
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
				RemoteShutter.lowVoltStateOrBadConditions = value;
			else
				RemoteShutter.lowVoltStateOrBadConditions = "";
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
				RemoteShutter.volts = sVolts.toDouble();
				RemoteShutter.voltsCutOff = sVoltsCutOff.toDouble();
			}
			break;


		case WATCHDOG_INTERVAL:
			if (hasValue)
				RemoteShutter.watchdogInterval = value.toInt();
			break;

		case SHUTTER_PING:
			bShutterPresent = true;
			if (hasValue)
				RemoteShutter.lowVoltStateOrBadConditions = value;
			else
				RemoteShutter.lowVoltStateOrBadConditions = "";

			break;

		 case SHUTTER_RESTORE_MOTOR_DEFAULT:
			break;

		case SHUTTER_SSID:
			if (hasValue)
				 RemoteShutter.ssid = value;
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
		// shutterClient.flush();
		ReceiveWiFi(shutterClient);
	}
#endif
}
