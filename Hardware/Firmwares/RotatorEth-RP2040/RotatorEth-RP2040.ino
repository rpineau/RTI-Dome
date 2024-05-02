//
// RTI-Zone Dome Rotator firmware.
// for Raspberry Pi Pico W (RP2040)
//

// Uncomment #define DEBUG to enable printing debug messages in serial


#include "Arduino.h"

#define DEBUG   // enable debug to DebugPort serial port
#ifdef DEBUG
#define DebugPort Serial    // Programming port
#endif


#ifdef DEBUG
#pragma message "Debug messages enabled"
#define DBPrint(x) if(DebugPort) DebugPort.print(x)
#define DBPrintln(x) if(DebugPort) DebugPort.println(x)
#define DBPrintHex(x) if(DebugPort) DebugPort.print(x, HEX)
#else
#pragma message "Debug messages disabled"
#define DBPrint(x)
#define DBPrintln(x)
#define DBPrintHex(x)
#endif // DEBUG

#define MAX_TIMEOUT 10

#define VERSION "2.645"

#define USE_EXT_EEPROM
#define USE_ETHERNET
#define USE_ALPACA
#define USE_WIFI

// if uncommented, USE_WIFI will enable all code related to the shutter over WiFi.
// This is useful for people who only want to automate the rotation.
#ifdef USE_WIFI
#pragma message "Local WiFi shutter enable"
#endif

#define Computer Serial2     // USB FTDI
#define FTDI_RESET  28
String IpAddress2String(const IPAddress& ipAddress)
{
  return String(ipAddress[0]) + String(".") +
  		String(ipAddress[1]) + String(".") +
		String(ipAddress[2]) + String(".") +
		String(ipAddress[3]); 
}
#include "RotatorClass.h"

#ifdef USE_ETHERNET
#pragma message "Ethernet enabled"
// include and some defines for ethernet connection
#include <SPI.h>    // RP2040 :  SCK: GP18, COPI/TX: GP19, CIPO/RX: GP16, CS: GP17
#include <W5500lwIP.h>
#include "EtherMac.h"
// RP2040 SPI CS
#define ETHERNET_CS     17
#define ETHERNET_INT	27
#define ETHERNET_RESET  20
Wiznet5500lwIP domeEthernet(ETHERNET_CS, SPI, ETHERNET_INT);
#define EthernetClient WiFiClient
uint32_t uidBuffer[4];  // Board unique ID
byte MAC_Address[6];    // Mac address, uses part of the unique ID
#define CMD_SERVER_PORT 2323
// global variable for the IP config and to check if we detect the ethernet card
std::atomic<bool> ethernetPresent;
IPConfig ServerConfig;
WiFiServer *domeServer = nullptr;
EthernetClient domeClient;
int nbEthernetClient;
String networkBuffer;
String sLocalIPAdress;
#endif // USE_ETHERNET

#ifdef USE_WIFI
#include "RemoteShutterClass.h"
RemoteShutterClass RemoteShutter;
#include <WiFi.h>
#define SHUTTER_PORT 2424
std::atomic<bool> wifiPresent;
WIFIConfig wifiConfig;
WiFiServer *shutterServer = nullptr;
WiFiClient shutterClient;
String wifiBuffer;
int nbWiFiClient;
String sLocalWifiIPAddress;
std::atomic<bool> bGotHelloFromShutter;
#endif

String computerBuffer;

std::atomic<bool> core0Ready = false;

bool bParked = false; // use to the run check doesn't continuously try to park

RotatorClass *Rotator = NULL;

static const unsigned long pingInterval = 15000; // 15 seconds, can't be changed with command

// Once booting is done and XBee is ready, broadcast a hello message
// so a shutter knows you're around if it is already running. If not,
// the shutter will send a hello when it boots.
std::atomic<bool> bSentHello = false;

#ifdef USE_WIFI
// Timer to periodically ping the shutter
StopWatch PingTimer;
StopWatch ShutterWatchdog;
#endif

std::atomic<bool> bShutterPresent = false;
// global variable for rain status
std::atomic<bool> bIsRaining = false;
// global variable for shutter voltage state
std::atomic<bool> bLowShutterVoltage = false;

#ifdef USE_ETHERNET
#endif // USE_ETHERNET

const char ERR_NO_DATA = -1;

#include "dome_commands.h"
enum CmdSource {SERIAL_CMD, NETWORK_CMD};
// function prototypes
#ifdef USE_ETHERNET
void configureEthernet();
bool initEthernet(bool bUseDHCP, IPAddress ip, IPAddress dns, IPAddress gateway, IPAddress subnet);
void checkForNewTCPClient();
#endif // USE_ETHERNET
#ifdef USE_WIFI
void configureWiFi();
bool initWiFi(IPAddress ip, String sSSID, String sPassword);
void checkForNewWifiClient();
#endif
void homeIntHandler();
void rainIntHandler();
void buttonHandler();
void resetChip(int);
void resetFTDI(int);
void StartWirelessConfig();
void ConfigXBee();
void setPANID(String);
void SendHello();
void requestShutterData();
void CheckForCommands();
void CheckForRain();
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

#ifdef USE_ALPACA
#include "AlpacaAPI.h"
DomeAlpacaServer *AlpacaServer;
DomeAlpacaDiscoveryServer *AlpacaDiscoveryServer;
#endif // USE_ALPACA


//
// Setup and main loops
//
void setup()
{
	core0Ready = false;
	nbWiFiClient = 0;
	nbEthernetClient = 0;
	digitalWrite(FTDI_RESET, 0);
	pinMode(FTDI_RESET, OUTPUT);

#ifdef USE_ETHERNET
	domeEthernet.setSPISpeed(30000000);
	lwipPollingPeriod(3);
	digitalWrite(ETHERNET_RESET, 0);
	pinMode(ETHERNET_RESET, OUTPUT);
#endif // USE_ETHERNET

	resetFTDI(FTDI_RESET);

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


	Rotator = new RotatorClass();
	Rotator->motorStop();
	Rotator->Stop();
	Rotator->EnableMotor(false);
	
#ifdef DEBUG
	DebugPort.begin(115200);
	DBPrintln("========== RTI-Zone controller booting ==========");
#endif

#ifdef USE_WIFI
	bSentHello = false;
	bGotHelloFromShutter = false;
	configureWiFi();
#endif 

#ifdef USE_ETHERNET
	configureEthernet();
#ifdef USE_ALPACA
	AlpacaDiscoveryServer = new DomeAlpacaDiscoveryServer();
	AlpacaServer = new DomeAlpacaServer();
	AlpacaDiscoveryServer->startServer();
	AlpacaServer->startServer();
#endif // USE_ALPACA
#endif // USE_ETHERNET
#ifdef DEBUG
	Computer.println("Online");
#endif

	core0Ready = true;
	DBPrintln("========== Core 0 ready ==========");
}

void setup1()
{
	while(!core0Ready)
		delay(100);

	DBPrintln("========== Core 1 starting ==========");

	DBPrintln("========== Core 1 Attaching interrupt handler ==========");
	attachInterrupt(digitalPinToInterrupt(HOME_PIN), homeIntHandler, FALLING);
	attachInterrupt(digitalPinToInterrupt(RAIN_SENSOR_PIN), rainIntHandler, CHANGE);
	attachInterrupt(digitalPinToInterrupt(BUTTON_CW), buttonHandler, CHANGE);
	attachInterrupt(digitalPinToInterrupt(BUTTON_CCW), buttonHandler, CHANGE);

	DBPrintln("========== Core 1 ready ==========");
}

//
// This loop takes care of all communications and commands
//
void loop()
{

#ifdef USE_ETHERNET
	if(ethernetPresent) {
		checkForNewTCPClient();
#ifdef USE_ALPACA
		AlpacaDiscoveryServer->checkForRequest();
		AlpacaServer->checkForRequest();
#endif
	}
#endif // USE_ETHERNET

#ifdef USE_WIFI
	if(wifiPresent)
		checkForNewWifiClient();
#endif
	CheckForCommands();
	CheckForRain();

#ifdef USE_WIFI
	if(wifiPresent) {
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
#endif
}

//
// This loop does all the motor controls
//
void loop1()
{   // all stepper motor code runs on core 1
	Rotator->Run();
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
	nbEthernetClient = 0;
	// Ethernet.init(ETHERNET_CS);
	domeEthernet.setHostname("RTI-Dome");

	DBPrintln("========== Setting IP config ==========");
	// try DHCP if set
	if(bUseDHCP) {
		domeEthernet.config(INADDR_NONE);
		bDhcpOk = domeEthernet.begin(MAC_Address);
		while (domeEthernet.status() == WL_DISCONNECTED) {
			DBPrintln("Waiting for DHCP");
			delay(1000);
			nTimeout++;
			if(nTimeout>15) { // 15 seconds should be plenty
				bDhcpOk = false;
				break;
			}
		}
		if(!bDhcpOk) {
			DBPrintln("DHCP Failed!");
			if(domeEthernet.linkStatus() == LinkON ) {
				domeEthernet.config(ip, dns, gateway, subnet);
				domeEthernet.begin(MAC_Address);
			}
			else {
				DBPrintln("No cable");
				return false;
			}
		}
	}
	else {
		domeEthernet.config(ip, dns, gateway, subnet);
		domeEthernet.begin(MAC_Address);
	}

	DBPrintln("========== Checking hardware status ==========");
	if(domeEthernet.status() == WL_NO_SHIELD) {
		 DBPrintln("NO HARDWARE !!!");
		return false;
	}
	DBPrintln("W5500 Ok.");

	DBPrintln("IP = " + IpAddress2String(domeEthernet.localIP()));
	domeServer = new WiFiServer(domeEthernet.localIP(), CMD_SERVER_PORT);
	domeServer->begin();
	domeServer->setNoDelay(true);
	DBPrintln("Server ready");
	return true;
}


void checkForNewTCPClient()
{
	//if(ServerConfig.bUseDHCP)
	//	domeEthernet.maintain();
	if(!domeServer)
		return;
		
	EthernetClient newClient = domeServer->accept();
	if(newClient) {
		DBPrintln("new client");
		if(nbEthernetClient > 0) { // we only accept 1 client
			newClient.print("Already in use#");
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
		// configureEthernet();
	}
}
#endif // USE_ETHERNET

#ifdef USE_WIFI
void configureWiFi()
{
	DBPrintln("========== Configuring WiFi ==========");
	Rotator->getWiFiConfig(wifiConfig);

	wifiPresent = initWiFi(wifiConfig.ip,
								String(wifiConfig.sSSID),
								String(wifiConfig.sPassword));
}

bool initWiFi(IPAddress ip, String sSSID, String sPassword)
{
	WiFi.mode(WIFI_AP);
	WiFi.setHostname("RTI-Dome");
	WiFi.config(ip);
	WiFi.beginAP(sSSID.c_str(), sPassword.c_str());
	if(WiFi.status() != WL_CONNECTED) {
		DBPrintln("========== Failed to start WiFi AP ==========");
		return false;
	}
	DBPrintln("IP = " + IpAddress2String(WiFi.localIP()));

	shutterServer = new WiFiServer(ip,SHUTTER_PORT);
	shutterServer->begin();
	shutterServer->setNoDelay(true);
	return true;
}

void checkForNewWifiClient()
{
	if(!shutterServer)
		return;

	WiFiClient newClient = shutterServer->accept();
	if(newClient) {
		DBPrintln("new WiFi client");
		if(nbWiFiClient > 0) { // we only accept 1 client
			newClient.print("Already in use#");
			newClient.flush();
			newClient.stop();
			DBPrintln("new client rejected");
		}
		else {
			nbWiFiClient++;
			shutterClient = newClient;
			DBPrintln("new wiFi client accepted");
			DBPrintln("nb WiFi client = " + String(nbWiFiClient));
			SendHello();
		}
	}

	if((nbWiFiClient>0) && !shutterClient.connected()) {
		DBPrintln("WiFi client disconnected");
		shutterClient.stop();
		nbWiFiClient--;
	}
}

#endif
void homeIntHandler()
{
   if(Rotator)
	   Rotator->homeInterrupt();
}

void rainIntHandler()
{
   if(Rotator)
	   Rotator->rainInterrupt();
}

void buttonHandler()
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

//reset FTDI FT232 usb to serial chip
void resetFTDI(int nPin)
{
	digitalWrite(nPin,0);
	delay(1000);
	digitalWrite(nPin,1);
}



#ifdef USE_WIFI

void SendHello()
{
	if(nbWiFiClient && shutterClient.connected()) {
		DBPrintln("Sending hello");
		shutterClient.print(String(HELLO) + "#");
		shutterClient.flush();
		ReceiveWiFi(shutterClient);
		bSentHello = true;
	}
}

void requestWiFiShutterData()
{
	if(nbWiFiClient && shutterClient.connected()) {
		shutterClient.print(String(STATE_SHUTTER) + "#");
		shutterClient.flush();
		ReceiveWiFi(shutterClient);

		shutterClient.print(String(VERSION_SHUTTER) + "#");
		shutterClient.flush();
		ReceiveWiFi(shutterClient);

		shutterClient.print(String(REVERSED_SHUTTER) + "#");
		shutterClient.flush();
		ReceiveWiFi(shutterClient);

		shutterClient.print(String(STEPSPER_SHUTTER) + "#");
		shutterClient.flush();
		ReceiveWiFi(shutterClient);

		shutterClient.print(String(SPEED_SHUTTER) + "#");
		shutterClient.flush();
		ReceiveWiFi(shutterClient);

		shutterClient.print(String(ACCELERATION_SHUTTER) + "#");
		shutterClient.flush();
		ReceiveWiFi(shutterClient);

		shutterClient.print(String(VOLTS_SHUTTER) + "#");
		shutterClient.flush();
		ReceiveWiFi(shutterClient);

		shutterClient.print(String(SHUTTER_PANID) + "#");
		shutterClient.flush();
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

void CheckForRain()
{
	int nPosition, nParkPos;
	if(bIsRaining != Rotator->GetRainStatus()) { // was there a state change ?
		bIsRaining = Rotator->GetRainStatus();
#ifdef USE_WIFI
		shutterClient.print(String(RAIN_SHUTTER) + String(bIsRaining ? "1" : "0") + "#");
		shutterClient.flush();
		ReceiveWiFi(shutterClient);
#endif // USE_WIFI
	}
	if (bIsRaining) {
		if (Rotator->GetRainAction() == HOME && Rotator->GetHomeStatus() != ATHOME) {
			DBPrintln("Raining- > Homing");
			Rotator->StartHoming();
		}

		if (Rotator->GetRainAction() == PARK && !bParked) {
			nParkPos = Rotator->GetParkAzimuth();
			DBPrintln("Raining -> Parking");
			Rotator->GoToAzimuth(nParkPos);
			bParked = true;
		}
	// keep telling the shutter that it's raining
#ifdef USE_WIFI
		shutterClient.print(String(RAIN_SHUTTER) + String(bIsRaining ? "1" : "0") + "#");
		shutterClient.flush();
#endif // USE_WIFI
	}
}

#ifdef USE_WIFI

void checkShuterLowVoltage()
{
	bLowShutterVoltage = (RemoteShutter.lowVoltStateOrRaining.equals("L"));
	if(bLowShutterVoltage) {
		 Rotator->GoToAzimuth(Rotator->GetParkAzimuth()); // we need to park so we can recharge the shutter battery
		 bParked = true;
	}
}

void PingWiFiShutter()
{
	if(PingTimer.elapsed() >= pingInterval) {
		if(nbWiFiClient && shutterClient.connected()) {
			shutterClient.print(String(SHUTTER_PING) + "#");
			shutterClient.flush();
			ReceiveWiFi(shutterClient);
			PingTimer.reset();
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

// All comms are terminated with '#' but the '\r' and '\n' are for XBee config
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
	double fTmp;
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
			Rotator->Stop();
#ifdef USE_WIFI
			if(nbWiFiClient && shutterClient.connected()) {
				shutterMessage = sTmpString;
				shutterClient.print(shutterMessage + "#");
				shutterClient.flush();
				ReceiveWiFi(shutterClient);
			}
#endif // USE_WIFI
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
				fTmp = value.toDouble();
				while(fTmp < 0) {
					fTmp+=360.0;
				}
				while(fTmp > 360) {
					fTmp-=360.0;
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
				if ((fTmp >= 0) && (fTmp < 360))
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
				if ((fTmp >= 0) && (fTmp < 360)) {
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

		case RAIN_ROTATOR_ACTION:
			if (hasValue) {
				Rotator->SetRainAction(value.toInt());
			}
			serialMessage = String(RAIN_ROTATOR_ACTION) + String(Rotator->GetRainAction());
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
				if (fTmp >= 0 && fTmp < 360) {
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

		case RAIN_SHUTTER:
			serialMessage = String(RAIN_SHUTTER) + String(bIsRaining ? "1" : "0");
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
			domeServer->stop();
			delete domeServer;
			domeServer = nullptr;
			domeEthernet.end();
			configureEthernet();
			serialMessage = String(ETH_RECONFIG)  + String(ethernetPresent?"1":"0");
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
				serialMessage = String(IP_ADDRESS) + String(IpAddress2String(domeEthernet.localIP()));
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
				serialMessage = String(IP_SUBNET) + String(IpAddress2String(domeEthernet.subnetMask()));
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
				serialMessage = String(IP_GATEWAY) + String(IpAddress2String(domeEthernet.gatewayIP()));
			}
			break;
#endif // USE_ETHERNET

#ifdef USE_WIFI
		case SHUTTER_PING:
			shutterMessage = String(SHUTTER_PING);
			if(nbWiFiClient && shutterClient.connected()) {
				shutterClient.print(shutterMessage + "#");
				shutterClient.flush();
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
				shutterClient.print(shutterMessage + "#");
				shutterClient.flush();
				ReceiveWiFi(shutterClient);
			}
			serialMessage = sTmpString + String(RemoteShutter.acceleration);
			break;

		case CLOSE_SHUTTER:
			sTmpString = String(CLOSE_SHUTTER);
			if(nbWiFiClient && shutterClient.connected()) {
				shutterClient.print(sTmpString+ "#");
				shutterClient.flush();
				ReceiveWiFi(shutterClient);
			}
			serialMessage = sTmpString;
			break;

		case SHUTTER_RESTORE_MOTOR_DEFAULT :
			sTmpString = String(SHUTTER_RESTORE_MOTOR_DEFAULT);
			if(nbWiFiClient && shutterClient.connected()) {
				shutterClient.print(sTmpString+ "#");
				shutterClient.flush();
				ReceiveWiFi(shutterClient);
				shutterClient.print(String(SPEED_SHUTTER)+ "#");
				shutterClient.flush();
				ReceiveWiFi(shutterClient);
				shutterClient.print(String(ACCELERATION_SHUTTER)+ "#");
				shutterClient.flush();
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
//          shutterClient.print(wirelessMessage + "#");
//          ReceiveWiFi(shutterClient);
//          serialMessage = sTmpString + RemoteShutter.position;
//          break;

		case OPEN_SHUTTER:
			sTmpString = String(OPEN_SHUTTER);
			if(nbWiFiClient && shutterClient.connected()) {
				shutterClient.print(sTmpString + "#");
				shutterClient.flush();
				ReceiveWiFi(shutterClient);
				}
			serialMessage = sTmpString + RemoteShutter.lowVoltStateOrRaining;
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
				shutterClient.print(shutterMessage + "#");
				shutterClient.flush();
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
				shutterClient.print(shutterMessage + "#");
				shutterClient.flush();
				ReceiveWiFi(shutterClient);
			}
			serialMessage = sTmpString + RemoteShutter.speed;
			break;

		case STATE_SHUTTER:
			sTmpString = String(STATE_SHUTTER);
			if(nbWiFiClient && shutterClient.connected()) {
				shutterClient.print(sTmpString + "#");
				shutterClient.flush();
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
				shutterClient.print(shutterMessage + "#");
				shutterClient.flush();
				ReceiveWiFi(shutterClient);
			}
			serialMessage = sTmpString + String(RemoteShutter.stepsPerStroke);
			break;

		case VERSION_SHUTTER:
			sTmpString = String(VERSION_SHUTTER);
			if(nbWiFiClient && shutterClient.connected()) {
				shutterClient.print(sTmpString + "#");
				shutterClient.flush();
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
				shutterClient.print(shutterMessage + "#");
				shutterClient.flush();
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
				shutterClient.print(shutterMessage + "#");
				shutterClient.flush();
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

		switch(nSource) {
			case SERIAL_CMD:
				if(Computer) {
					Computer.print(serialMessage + "#");
				}
				break;
	#ifdef USE_ETHERNET
			case NETWORK_CMD:
				if(domeClient.connected()) {
					DBPrintln("Network serialMessage = " + serialMessage);
					domeClient.print(serialMessage + "#");
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

		case RAIN_SHUTTER:
			shutterClient.print(String(RAIN_SHUTTER) + String(bIsRaining ? "1" : "0") + "#");
			shutterClient.flush();
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
				RemoteShutter.lowVoltStateOrRaining = value;
			else
				RemoteShutter.lowVoltStateOrRaining = "";
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
				RemoteShutter.lowVoltStateOrRaining = value;
			else
				RemoteShutter.lowVoltStateOrRaining = "";

			break;

		 case SHUTTER_RESTORE_MOTOR_DEFAULT:
			break;

		case SHUTTER_PANID:
			if (hasValue)
				 RemoteShutter.panid = value;
			break;

		default:
			break;
	}

}
#endif