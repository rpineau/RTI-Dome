//
// RTI-Zone Dome Rotator firmware.
//
//  Copyright © 2018 Rodolphe Pineau. All rights reserved.
//

// Uncomment #define DEBUG in RotatorClass.h to enable printing debug messages in serial

// if uncommented, STANDALONE will disable all code related to the XBee and the shutter.
// This us useful for people who only want to automate the rotation.
// #define STANDALONE

#ifdef STANDALONE
#pragma message "Standalone mode, no shutter code"
#endif // STANDALONE

// The Xbee S1 were the original one used on the NexDome controller.
// I have since tested with a pair of S2C that are easier to find and
// fix the Xbee init command to make it work.
// Also the XBee3 model XB3-24Z8PT-J work as the S1
#define XBEE_S1
// #define XBEE_S2C

#define MAX_TIMEOUT 10
#define ERR_NO_DATA -1
#define OK  0

#define VERSION "2.645"

#define USE_EXT_EEPROM
#define USE_ETHERNET

#ifdef USE_ETHERNET
#pragma message "Ethernet enabled"
// include and some defines for ethernet connection
#include <SPI.h>
#include <Ethernet.h>
#include "EtherMac.h"
#endif // USE_ETHERNET

#define Computer Serial2     // USB FTDI

#define FTDI_RESET  23
#ifndef STANDALONE
#define Wireless Serial1    // Serial1 on pin 18/19 for XBEE
#endif // STANDALONE
#define DebugPort Serial   // Programming port

#include "RotatorClass.h"

#ifdef USE_ETHERNET
#define ETHERNET_CS     52
#define ETHERNET_RESET  53
uint32_t uidBuffer[4];  // DUE unique ID
byte MAC_Address[6];    // Mac address, uses part of the unique ID

#define SERVER_PORT 2323
EthernetServer domeServer(SERVER_PORT);
EthernetClient domeClient;
int nbEthernetClient;
String networkBuffer;
#endif // USE_ETHERNET

String computerBuffer;


#ifndef STANDALONE
#define XBEE_RESET  8
#include "RemoteShutterClass.h"
RemoteShutterClass RemoteShutter;
String wirelessBuffer;
bool XbeeStarted, sentHello, isConfiguringWireless, gotHelloFromShutter;
int configStep = 0;
bool isResetingXbee = false;
int XbeeResets = 0;
#endif // STANDALONE

bool bParked = false; // use to the rin check doesn't continuously try to park

RotatorClass *Rotator = NULL;

//
// XBee init AT commands
///
#ifndef STANDALONE
#if defined(XBEE_S1)
#define NB_AT_OK  17
/// ATAC,CE1,ID4242,CH0C,MY0,DH0,DLFFFF,RR6,RN2,PL4,AP0,SM0,BD3,WR,FR,CN
String ATString[18] = {"ATRE","ATWR","ATAC","ATCE1","","ATCH0C","ATMY0","ATDH0","ATDLFFFF",
						"ATRR6","ATRN2","ATPL4","ATAP0","ATSM0","ATBD3","ATWR","ATFR","ATCN"};
#endif
#if defined(XBEE_S2C)
#define NB_AT_OK  13
/// ATAC,CE1,ID4242,DH0,DLFFFF,PL4,AP0,SM0,BD3,WR,FR,CN
String ATString[18] = {"ATRE","ATWR","ATAC","ATCE1","","ATDH0","ATDLFFFF",
						"ATPL4","ATAP0","ATSM0","ATBD3","ATWR","ATFR","ATCN"};
#endif

// index in array above where the command is empty.
// This allows us to change the Pan ID and store it in the EEPROM/Flash
#define PANID_STEP 4

static const unsigned long pingInterval = 15000; // 15 seconds, can't be changed with command

#define MAX_XBEE_RESET  10
// Once booting is done and XBee is ready, broadcast a hello message
// so a shutter knows you're around if it is already running. If not,
// the shutter will send a hello when it boots.
volatile  bool SentHello = false;

// Timer to periodically ping the shutter
StopWatch PingTimer;
StopWatch ShutterWatchdog;
#endif // STANDALONE

volatile bool bShutterPresent = false;
// global variable for rain status
volatile bool bIsRaining = false;
// global variable for shutter voltage state
volatile bool bLowShutterVoltage = false;

#ifdef USE_ETHERNET
// global variable for the IP config and to check if we detect the ethernet card
bool ethernetPresent;
IPConfig ServerConfig;
#endif // USE_ETHERNET

#include "dome_commands.h"

// function prototypes
#ifdef USE_ETHERNET
void configureEthernet();
bool initEthernet(bool bUseDHCP, IPAddress ip, IPAddress dns, IPAddress gateway, IPAddress subnet);
void checkForNewTCPClient();
#endif // USE_ETHERNET
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
#ifndef STANDALONE
void checkShuterLowVoltage();
#endif // STANDALONE
void PingShutter();
#ifdef USE_ETHERNET
void ReceiveNetwork(EthernetClient);
#endif // USE_ETHERNET
void ReceiveComputer();
void ProcessCommand();
void ReceiveWireless();
void ProcessWireless();

void setup()
{
#ifndef STANDALONE
	// set reset pins to output and low
	digitalWrite(XBEE_RESET, 0);
	pinMode(XBEE_RESET, OUTPUT);
#endif // STANDALONE
	digitalWrite(FTDI_RESET, 0);
	pinMode(FTDI_RESET, OUTPUT);

#ifdef USE_ETHERNET
	digitalWrite(ETHERNET_RESET, 0);
	pinMode(ETHERNET_RESET, OUTPUT);
#endif // USE_ETHERNET

#ifndef STANDALONE
	resetChip(XBEE_RESET);
#endif // STANDALONE
	resetFTDI(FTDI_RESET);

#ifdef DEBUG
	DebugPort.begin(115200);
	DBPrintln("\n\n========== RTI-Zome controller booting ==========\n\n");
#endif
#ifdef USE_ETHERNET
	getMacAddress(MAC_Address, uidBuffer);
#endif // USE_ETHERNET

	Computer.begin(115200);

#ifndef STANDALONE
	Wireless.begin(9600);
	PingTimer.reset();
	XbeeStarted = false;
	sentHello = false;
	isConfiguringWireless = false;
	gotHelloFromShutter = false;
#endif // STANDALONE
	Rotator = new RotatorClass();
	Rotator->motorStop();
	Rotator->EnableMotor(false);
	noInterrupts();
	attachInterrupt(digitalPinToInterrupt(HOME_PIN), homeIntHandler, FALLING);
	attachInterrupt(digitalPinToInterrupt(RAIN_SENSOR_PIN), rainIntHandler, CHANGE);
	attachInterrupt(digitalPinToInterrupt(BUTTON_CW), buttonHandler, CHANGE);
	attachInterrupt(digitalPinToInterrupt(BUTTON_CCW), buttonHandler, CHANGE);
	interrupts();
#ifdef USE_ETHERNET
	configureEthernet();
#endif // USE_ETHERNET
}

void loop()
{

#ifdef USE_ETHERNET
	if(ethernetPresent)
		checkForNewTCPClient();
#endif // USE_ETHERNET

#ifndef STANDALONE
	if (!XbeeStarted) {
		if (!isConfiguringWireless) {
			DBPrintln("Xbee reconfiguring");
			StartWirelessConfig();
			DBPrintln("isConfiguringWireless : " + String(isConfiguringWireless));
		}
	}
#endif // STANDALONE
	Rotator->Run();
	CheckForCommands();
	CheckForRain();
#ifndef STANDALONE
	checkShuterLowVoltage();
	if(XbeeStarted) {
		if(ShutterWatchdog.elapsed() > (pingInterval*5) && XbeeResets < MAX_XBEE_RESET) { // try 10 times max
			// lets try to recover
			if(!isResetingXbee && XbeeResets < MAX_XBEE_RESET) {
				DBPrintln("watchdogTimer triggered");
				DBPrintln("Resetting XBee reset #" + String(XbeeResets));
				bShutterPresent = false;
				SentHello = false;
				XbeeResets++;
				isResetingXbee = true;
				resetChip(XBEE_RESET);
				isConfiguringWireless = false;
				XbeeStarted = false;
				configStep = 0;
				StartWirelessConfig();
			}
		}
		else if(!SentHello && XbeeResets < MAX_XBEE_RESET) // if after 10 reset we didn't get an answer there is no point sending more hello.
			SendHello();
		else
			PingShutter();

		if(gotHelloFromShutter) {
			requestShutterData();
			gotHelloFromShutter = false;
		}
	}
#endif // STANDALONE
}

#ifdef USE_ETHERNET
void configureEthernet()
{
	Rotator->getIpConfig(ServerConfig);
	ethernetPresent =  initEthernet(ServerConfig.bUseDHCP,
									ServerConfig.ip,
									ServerConfig.dns,
									ServerConfig.gateway,
									ServerConfig.subnet);
}


bool initEthernet(bool bUseDHCP, IPAddress ip, IPAddress dns, IPAddress gateway, IPAddress subnet)
{
	int dhcpOk;
#ifdef DEBUG
	IPAddress aTmp;
#endif

	resetChip(ETHERNET_RESET);
	// network configuration
	nbEthernetClient = 0;
	Ethernet.init(ETHERNET_CS);

	// try DHCP if set
	if(bUseDHCP) {
		dhcpOk = Ethernet.begin(MAC_Address, 10000, 4000); // short timeout
		if(!dhcpOk) {
			DBPrintln("DHCP Failed!");
			if(Ethernet.linkStatus() == LinkON )
				Ethernet.begin(MAC_Address, ip, dns, gateway, subnet);
			else {
				DBPrintln("No cable");
				return false;
			}
		}
	}
	else {
		Ethernet.begin(MAC_Address, ip, dns, gateway, subnet);
	}

	if(Ethernet.hardwareStatus() == EthernetNoHardware) {
		 DBPrintln("NO HARDWARE !!!");
		return false;
	}
#ifdef DEBUG
	aTmp = Ethernet.localIP();
	DBPrintln("IP = " + String(aTmp[0]) + String(".") +
						String(aTmp[1]) + String(".") +
						String(aTmp[2]) + String(".") +
						String(aTmp[3]) );
#endif

	Ethernet.setRetransmissionCount(3);

	DBPrintln("Server ready, calling begin()");
	domeServer.begin();
	return true;
}


void checkForNewTCPClient()
{
	if(ServerConfig.bUseDHCP)
		Ethernet.maintain();

	EthernetClient newClient = domeServer.accept();
	if(newClient) {
		DBPrintln("new client");
		if(nbEthernetClient > 0) { // we only accept 1 client
			newClient.write("Already in use#\n");
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
		// domeClient.stop();
		nbEthernetClient--;
		configureEthernet();
	}
}
#endif // USE_ETHERNET

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

#ifndef STANDALONE
void StartWirelessConfig()
{
	DBPrintln("Xbee configuration started");
	delay(1100); // guard time before and after
	isConfiguringWireless = true;
	DBPrintln("Sending +++");
	Wireless.write("+++");
	delay(1100);
	ShutterWatchdog.reset();
}

inline void ConfigXBee()
{

	DBPrintln("Sending ");
	if ( configStep == PANID_STEP) {
		String ATCmd = "ATID" + String(Rotator->GetPANID());
		DBPrintln(ATCmd);
		Wireless.println(ATCmd.c_str());
		configStep++;
	}
	else {
		String ATCmd = ATString[configStep];
		DBPrintln(ATCmd);
		Wireless.println(ATCmd.c_str());
		configStep++;
	}
	if (configStep > NB_AT_OK) {
		isConfiguringWireless = false;
		XbeeStarted = true;
		Rotator->SaveToEEProm();
		DBPrintln("Xbee configuration finished");
		while(Wireless.available() > 0) {
			Wireless.read();
		}
		SentHello = false;
		gotHelloFromShutter = false;
		isResetingXbee = false;
	}
	delay(100);
}

void setPANID(String value)
{
	Rotator->setPANID(value);
	resetChip(XBEE_RESET);
	isConfiguringWireless = false;
	XbeeStarted = false;
	configStep = 0;
}

void SendHello()
{
	String sCmd;
	DBPrintln("Sending hello");
	sCmd = String(HELLO) + "#";
	Wireless.write(sCmd.c_str());
	ReceiveWireless();
	SentHello = true;
}

void requestShutterData()
{
	String sCmd;
	sCmd = String(STATE_SHUTTER) + "#";
	Wireless.write(sCmd.c_str());
	ReceiveWireless();

	sCmd = String(VERSION_SHUTTER) + "#";
	Wireless.write(sCmd.c_str());
	ReceiveWireless();

	sCmd = String(REVERSED_SHUTTER) + "#";
	Wireless.write(sCmd.c_str());
	ReceiveWireless();

	sCmd = String(STEPSPER_SHUTTER) + "#";
	Wireless.write(sCmd.c_str());
	ReceiveWireless();

	sCmd = String(SPEED_SHUTTER) + "#";
	Wireless.write(sCmd.c_str());
	ReceiveWireless();

	sCmd = String(ACCELERATION_SHUTTER) + "#";
	Wireless.write(sCmd.c_str());
	ReceiveWireless();

	sCmd = String(VOLTS_SHUTTER) + "#";
	Wireless.write(sCmd.c_str());
	ReceiveWireless();

	sCmd = String(SHUTTER_PANID) + "#";
	Wireless.write(sCmd.c_str());
	ReceiveWireless();
}

#endif // STANDALONE

void CheckForCommands()
{
	ReceiveComputer();

#ifndef STANDALONE
	if (Wireless.available() > 0) {
		ReceiveWireless();
	}
#endif // STANDALONE
#ifdef USE_ETHERNET
	if(ethernetPresent )
		ReceiveNetwork(domeClient);
#endif // USE_ETHERNET
}

void CheckForRain()
{
	int nPosition, nParkPos;
	String sCmd;

	if(bIsRaining != Rotator->GetRainStatus()) { // was there a state change ?
		bIsRaining = Rotator->GetRainStatus();
#ifndef STANDALONE
		sCmd = String(RAIN_SHUTTER) + String(bIsRaining ? "1" : "0") + "#";
		Wireless.write(sCmd.c_str());
		ReceiveWireless();
#endif // STANDALONE
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
#ifndef STANDALONE
		sCmd = String(RAIN_SHUTTER) + String(bIsRaining ? "1" : "0") + "#";
		Wireless.write(sCmd.c_str());
#endif // STANDALONE
	}
}

#ifndef STANDALONE

void checkShuterLowVoltage()
{
	bLowShutterVoltage = (RemoteShutter.lowVoltStateOrRaining.equals("L"));
	if(bLowShutterVoltage) {
		Rotator->GoToAzimuth(Rotator->GetParkAzimuth()); // we need to park so we can recharge tge shutter battery
		bParked = true;
	}
}

void PingShutter()
{
	String sCmd;

	if(PingTimer.elapsed() >= pingInterval) {
		sCmd = String(SHUTTER_PING) + "#";
		Wireless.write(sCmd.c_str());
		ReceiveWireless();
		PingTimer.reset();
		}
}
#endif // STANDALONE

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
					ProcessCommand(true);
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
					ProcessCommand(false);
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

void ProcessCommand(bool bFromNetwork)
{
	float fTmp;
	char command;
	String value;

#ifndef STANDALONE
	String wirelessMessage;
#endif // STANDALONE
	String serialMessage, sTmpString;
	bool hasValue = false;

	// Split the buffer into command char and value if present
	// Command character
	if(bFromNetwork) {
#ifdef USE_ETHERNET
		command = networkBuffer.charAt(0);
		// Payload
		value = networkBuffer.substring(1);
#endif // USE_ETHERNET
	}
	else {
		command = computerBuffer.charAt(0);
		// Payload
		value = computerBuffer.substring(1);
	}
	// payload has data
	if (value.length() > 0)
		hasValue = true;

	serialMessage = "";
#ifndef STANDALONE
	wirelessMessage = "";
#endif // STANDALONE

	DBPrintln("\nProcessCommand");
	DBPrintln("Command = \"" + String(command) +"\"");
	DBPrintln("Value = \"" + String(value) +"\"");
	DBPrintln("bFromNetwork = \"" + String(bFromNetwork?"Yes":"No") +"\"");


	switch (command) {
		case ABORT:
			sTmpString = String(ABORT);
			serialMessage = sTmpString;
			Rotator->Stop();
#ifndef STANDALONE
			wirelessMessage = sTmpString + "#";
			Wireless.write(wirelessMessage .c_str());
			ReceiveWireless();
#endif // STANDALONE
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
				if ((fTmp >= 0.0) && (fTmp <= 360.0)) {
					Rotator->GoToAzimuth(fTmp);
				}
				bParked = false;
			}
			serialMessage = String(GOTO_ROTATOR) + String(Rotator->GetAzimuth());
			break;
#ifndef STANDALONE
		case HELLO:
			SendHello();
			serialMessage = String(HELLO);
			break;
#endif // STANDALONE
		case HOME_ROTATOR:
			Rotator->StartHoming();
			serialMessage = String(HOME_ROTATOR);
			break;

		case HOMEAZ_ROTATOR:
			if (hasValue) {
				fTmp = value.toFloat();
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
				fTmp = value.toFloat();
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
				fTmp = value.toFloat();
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
				serialMessage = String(IP_ADDRESS) + String(Rotator->IpAddress2String(Ethernet.localIP()));
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
				serialMessage = String(IP_SUBNET) + String(Rotator->IpAddress2String(Ethernet.subnetMask()));
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
				serialMessage = String(IP_GATEWAY) + String(Rotator->IpAddress2String(Ethernet.gatewayIP()));
			}
			break;
#endif // USE_ETHERNET

#ifndef STANDALONE
		case INIT_XBEE:
			sTmpString = String(INIT_XBEE);
			isConfiguringWireless = false;
			XbeeStarted = false;
			configStep = 0;
			serialMessage = sTmpString;
			sTmpString += "#";
			Wireless.write(sTmpString.c_str());
			ReceiveWireless();
			DBPrintln("trying to reconfigure radio");
			resetChip(XBEE_RESET);
			break;

		case PANID:
			if (hasValue) {
				RemoteShutter.panid = "0000";
				wirelessMessage = String(SHUTTER_PANID) + value + "#";
				Wireless.write(wirelessMessage.c_str());
				setPANID(value); // shutter XBee should be doing the same thing
			}
			serialMessage = String(PANID) + String(Rotator->GetPANID());
			break;

		case SHUTTER_PANID:
			wirelessMessage = String(SHUTTER_PANID) + "#";
			Wireless.write(wirelessMessage.c_str());
			ReceiveWireless();
			serialMessage = String(SHUTTER_PANID) + RemoteShutter.panid ;
			break;


		case SHUTTER_PING:
			wirelessMessage = String(SHUTTER_PING) + "#";
			Wireless.write(wirelessMessage.c_str());
			ReceiveWireless();
			serialMessage = String(SHUTTER_PING);
			break;

		case ACCELERATION_SHUTTER:
			sTmpString = String(ACCELERATION_SHUTTER);
			if (hasValue) {
				RemoteShutter.acceleration = value;
				wirelessMessage = sTmpString + RemoteShutter.acceleration;
			}
			else {
				wirelessMessage = sTmpString;
			}
			wirelessMessage += "#";
			Wireless.write(wirelessMessage.c_str());
			ReceiveWireless();
			serialMessage = sTmpString + RemoteShutter.acceleration;
			break;

		case CLOSE_SHUTTER:
			serialMessage = String(CLOSE_SHUTTER);
			sTmpString = serialMessage + "#";
			Wireless.write(sTmpString.c_str());
			ReceiveWireless();
			break;

		case SHUTTER_RESTORE_MOTOR_DEFAULT :
			serialMessage =  String(SHUTTER_RESTORE_MOTOR_DEFAULT);
			sTmpString = serialMessage + "#";
			Wireless.write(sTmpString.c_str());
			ReceiveWireless();
			sTmpString = String(SPEED_SHUTTER) + "#";
			Wireless.write(sTmpString.c_str());
			ReceiveWireless();
			sTmpString = String(ACCELERATION_SHUTTER) + "#";
			Wireless.write(sTmpString.c_str());
			ReceiveWireless();
			break;

		case OPEN_SHUTTER:
				sTmpString = String(OPEN_SHUTTER) + "#";
				Wireless.write(sTmpString.c_str());
				ReceiveWireless();
				serialMessage = String(OPEN_SHUTTER) + RemoteShutter.lowVoltStateOrRaining;
				break;

		case REVERSED_SHUTTER:
			sTmpString = String(REVERSED_SHUTTER);
			if (hasValue) {
				RemoteShutter.reversed = value;
				wirelessMessage = sTmpString + value;
			}
			else {
				wirelessMessage = sTmpString;
			}
			wirelessMessage += "#";
			Wireless.write(wirelessMessage.c_str());
			ReceiveWireless();
			serialMessage = sTmpString + RemoteShutter.reversed;
			break;

		case SPEED_SHUTTER:
			sTmpString = String(SPEED_SHUTTER);
			if (hasValue) {
				RemoteShutter.speed = value;
				wirelessMessage = sTmpString + String(value.toInt());
			}
			else {
				wirelessMessage = sTmpString;
			}
			wirelessMessage += "#";
			Wireless.write(wirelessMessage.c_str());
			ReceiveWireless();
			serialMessage = sTmpString + RemoteShutter.speed;
			break;

		case STATE_SHUTTER:
			sTmpString = String(STATE_SHUTTER) + "#";
			Wireless.write(sTmpString.c_str());
			ReceiveWireless();
			serialMessage = String(STATE_SHUTTER) + RemoteShutter.state;
			break;

		case STEPSPER_SHUTTER:
			sTmpString = String(STEPSPER_SHUTTER);
			if (hasValue) {
				RemoteShutter.stepsPerStroke = value;
				wirelessMessage = sTmpString + value;
			}
			else {
				wirelessMessage = sTmpString;
			}
			wirelessMessage += "#";
			Wireless.write(wirelessMessage.c_str());
			ReceiveWireless();
			serialMessage = sTmpString + RemoteShutter.stepsPerStroke;
			break;

		case VERSION_SHUTTER:
			sTmpString = String(VERSION_SHUTTER) + "#";
			Wireless.write(sTmpString.c_str());
			ReceiveWireless();
			serialMessage = String(VERSION_SHUTTER) + RemoteShutter.version;
			break;

		case VOLTS_SHUTTER:
			sTmpString = String(VOLTS_SHUTTER);
			wirelessMessage = sTmpString;
			if (hasValue)
				wirelessMessage += String(value);
			wirelessMessage += "#";
			Wireless.write(wirelessMessage.c_str());
			ReceiveWireless();
			serialMessage = sTmpString + RemoteShutter.volts;
			break;

		case WATCHDOG_INTERVAL:
			sTmpString = String(WATCHDOG_INTERVAL);
			if (value.length() > 0) {
				wirelessMessage = sTmpString + value;
			}
			else {
				wirelessMessage = sTmpString;
			}
			wirelessMessage += "#";
			Wireless.write(wirelessMessage.c_str());
			ReceiveWireless();
			serialMessage = sTmpString + RemoteShutter.watchdogInterval;
			break;
#endif // STANDALONE

		default:
			serialMessage = "Unknown command:" + String(command);
			break;
	}


	// Send messages if they aren't empty.
	if (serialMessage.length() > 0) {
		if(!bFromNetwork) {
			if(Computer)
				serialMessage += "#";
				Computer.write(serialMessage.c_str());
				Computer.flush();
			}
#ifdef USE_ETHERNET
		else if(domeClient.connected()) {
				DBPrintln("Network serialMessage = " + serialMessage);
				serialMessage += "#";
				domeClient.write(serialMessage.c_str());
				domeClient.flush();
		}
#endif // USE_ETHERNET
	}
}


#ifndef STANDALONE

void ReceiveWireless()
{
	int timeout = 0;
	char wirelessCharacter;

	if (isConfiguringWireless) {
		DBPrintln("[ReceiveWireless] Configuring XBee");
		// read the response
		do {
			while(Wireless.available() < 1) {
				delay(1);
				timeout++;
				if(timeout >= MAX_TIMEOUT*10) {
					return;
					}
			}
			wirelessCharacter = Wireless.read();
			if (wirelessCharacter != ERR_NO_DATA) {
				if(wirelessCharacter != '\r' && wirelessCharacter != ERR_NO_DATA) {
					wirelessBuffer += String(wirelessCharacter);
				}
			}
		} while (wirelessCharacter != '\r');

		DBPrintln("[ReceiveWireless] wirelessBuffer = " + wirelessBuffer);

		ConfigXBee();
		wirelessBuffer = "";
		return;
	}

	// wait for response
	timeout = 0;
	while(Wireless.available() < 1) {
		delay(5);   // give time to the shutter to reply
		timeout++;
		if(timeout >= MAX_TIMEOUT) {
			return;
			}
	}

	// read the response
	timeout = 0;
	while(Wireless.available() > 0) {
		wirelessCharacter = Wireless.read();
		if (wirelessCharacter != ERR_NO_DATA) {
			if ( wirelessCharacter == '#') {
				// End of message
				if (wirelessBuffer.length() > 0) {
					ProcessWireless();
					wirelessBuffer = "";
					return; // we'll read the next response on the next loop.
				}
			}
			if(wirelessCharacter!=0xFF) {
				wirelessBuffer += String(wirelessCharacter);
			}
		} else {
			delay(5);   // give time to the shutter to send data as a character takes about 1ms at 9600
			timeout++;
		}
		if(timeout >= MAX_TIMEOUT) {
			return;
		}
	}
	return;
}

void ProcessWireless()
{
	char command;
	bool hasValue = false;
	String value;
	String sCmd;
	DBPrintln("<<< Received: '" + wirelessBuffer + "'");
	command = wirelessBuffer.charAt(0);
	value = wirelessBuffer.substring(1);
	if (value.length() > 0)
		hasValue = true;

	// we got data so the shutter is alive
	ShutterWatchdog.reset();
	bShutterPresent = true;
	XbeeResets = 0;

	switch (command) {
		case ACCELERATION_SHUTTER:
			if (hasValue)
				RemoteShutter.acceleration = value;
			break;

		case HELLO:
			gotHelloFromShutter = true;
			bShutterPresent = true;
			break;

		case SPEED_SHUTTER:
			if (hasValue)
				RemoteShutter.speed = value;
			break;

		case RAIN_SHUTTER:
			sCmd = String(RAIN_SHUTTER) + String(bIsRaining ? "1" : "0") + "#";
			Wireless.write(sCmd.c_str());
			break;

		case REVERSED_SHUTTER:
			if (hasValue)
				RemoteShutter.reversed = value;
			break;

		case STATE_SHUTTER:
			if (hasValue)
				RemoteShutter.state = value;
			break;

		case OPEN_SHUTTER:
			if (hasValue)
				RemoteShutter.lowVoltStateOrRaining = value;
			else
				RemoteShutter.lowVoltStateOrRaining = "";
			break;

		case STEPSPER_SHUTTER:
			if (hasValue)
				RemoteShutter.stepsPerStroke = value;
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
				RemoteShutter.sVoltsCutOff = sVoltsCutOff.toDouble();
			}
			break;


		case WATCHDOG_INTERVAL:
			if (hasValue)
				RemoteShutter.watchdogInterval = value;
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
#endif // STANDALONE
