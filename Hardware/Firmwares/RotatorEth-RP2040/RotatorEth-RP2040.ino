//
// RTI-Zone Dome Rotator firmware.
// Support Arduino DUE and RP2040
//

// Uncomment #define DEBUG to enable printing debug messages in serial

// if uncommented, STANDALONE will disable all code related to the XBee and the shutter.
// This us useful for people who only want to automate the rotation.

#include "Arduino.h"

#define DEBUG   // enable debug to DebugPort serial port
#ifdef DEBUG
#define DebugPort Serial    // Programming port
#endif

// #define STANDALONE
#ifdef STANDALONE
#pragma message "Standalone mode, no shutter code"
#endif // STANDALONE

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
#define OK  0

#define VERSION "2.645"

#define USE_EXT_EEPROM
#define USE_ETHERNET
//#define USE_ALPACA

#ifdef USE_ETHERNET
#pragma message "Ethernet enabled"
#ifdef USE_ALPACA
#pragma message "Alpaca server enabled"
#endif
// include and some defines for ethernet connection
#include <SPI.h>    // RP2040 :  SCK: GP18, COPI/TX: GP19, CIPO/RX: GP16, CS: GP17
#include <EthernetClient.h>
#include <Ethernet.h>
#include "EtherMac.h"
#endif // USE_ETHERNET

#if defined(ARDUINO_ARCH_RP2040)
#pragma message "RP2040 Serial2"
#endif
#define Computer Serial2     // USB FTDI

#if defined(ARDUINO_ARCH_RP2040)
#define FTDI_RESET  28
#else
#define FTDI_RESET  23
#endif

#ifndef STANDALONE
#define Wireless Serial1    // XBEE Serial1 on pin 18/19 for Arduino DUE , or 0/1 on RP2040
// The Xbee S1 were the original one used on the NexDome controller.
// I have since tested with a pair of S2C that are easier to find and
// fix the Xbee init command to make it work.
// Also the XBee3 model XB3-24Z8PT-J work as the S1
#define XBEE_S1
// #define XBEE_S2C
#endif // STANDALONE

#include "RotatorClass.h"

#ifdef USE_ETHERNET
#if defined(ARDUINO_ARCH_RP2040)    // RP2040 SPI
#define ETHERNET_CS     17
#define ETHERNET_RESET  20
#else
#define ETHERNET_CS     52
#define ETHERNET_RESET  53
#endif
uint32_t uidBuffer[4];  // Board unique ID (DUE, RP2040)
byte MAC_Address[6];    // Mac address, uses part of the unique ID

#define SERVER_PORT 2323
EthernetServer domeServer(SERVER_PORT);
#ifdef USE_ALPACA
#include "RTI-DomeAlpacaServer.h"
#define ALPACA_SERVER_PORT 11111
RTIDomeAlpacaServer AlpacaServer(ALPACA_SERVER_PORT);
#endif // USE_ALPACA
EthernetClient domeClient;
int nbEthernetClient;
String networkBuffer;
#endif // USE_ETHERNET

String computerBuffer;


#ifndef STANDALONE

#if defined(ARDUINO_ARCH_RP2040)
#define XBEE_RESET  22
#else
#define XBEE_RESET  8
#endif

#include "RemoteShutterClass.h"
RemoteShutterClass RemoteShutter;
String wirelessBuffer;
bool XbeeStarted, sentHello, isConfiguringWireless, gotHelloFromShutter;
int configStep = 0;
bool isResetingXbee = false;
int XbeeResets = 0;
#endif // STANDALONE

#if defined(ARDUINO_ARCH_RP2040)
volatile bool core0Ready = false;
#endif

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

const char ERR_NO_DATA = -1;

#include "dome_commands.h"
/*
// Rotator commands
const char ABORT_MOVE               = 'a'; // Tell everything to STOP!
const char ETH_RECONFIG                 = 'b'; // reconfigure ethernet
const char CALIBRATE_ROTATOR        = 'c'; // Calibrate the dome
const char RESTORE_MOTOR_DEFAULT        = 'd'; // restore default values for motor control.
const char ACCELERATION_ROTATOR     = 'e'; // Get/Set stepper acceleration
const char ETH_MAC_ADDRESS              = 'f'; // get the MAC address.
const char GOTO_ROTATOR             = 'g'; // Get dome azimuth or move to new position (GoT
const char HOME_ROTATOR             = 'h'; // Home the dome
const char HOMEAZ_ROTATOR           = 'i'; // Get/Set home position
const char IP_ADDRESS                   = 'j'; // get/set the IP address
const char VOLTS_ROTATOR            = 'k'; // Get volts and get/set cutoff
const char PARKAZ_ROTATOR           = 'l'; // Get/Set park azimuth
const char SLEW_ROTATOR             = 'm'; // Get Slewing status/direction
const char RAIN_ROTATOR_ACTION          = 'n'; // Get/Set action when rain sensor triggered (do nothing, home, park)
const char IS_SHUTTER_PRESENT           = 'o'; // check if the shutter has responded to pings
const char IP_SUBNET                    = 'p'; // get/set the ip subnet
const char PANID                    = 'q'; // get and set the XBEE PAN ID
const char SPEED_ROTATOR            = 'r'; // Get/Set step rate (speed)
const char SYNC_ROTATOR             = 's'; // Sync to new Azimuth
const char STEPSPER_ROTATOR         = 't'; // Get/set Steps per rotation
const char IP_GATEWAY                   = 'u'; // get/set default gateway IP
const char VERSION_ROTATOR          = 'v'; // Get Firmware Version
const char IP_DHCP                      = 'w'; // get/set DHCP mode
                                        //'x' see bellow
const char REVERSED_ROTATOR         = 'y'; // Get/Set stepper reversed status
const char HOMESTATUS_ROTATOR       = 'z'; // Get homed status

const char RAIN_SHUTTER             = 'F'; // Get rain status (from client) or tell shutter it's raining (from Rotator)

#ifndef STANDALONE
const char INIT_XBEE                    = 'x'; // force a XBee reconfig

// available A B J N S U W X Z
// Shutter commands
const char CLOSE_SHUTTER            = 'C'; // Close shutter
const char SHUTTER_RESTORE_MOTOR_DEFAULT= 'D'; // Restore default values for motor control.
const char ACCELERATION_SHUTTER     = 'E'; // Get/Set stepper acceleration
                                       // 'F' see above
//const char ELEVATION_SHUTTER      = 'G'; // Get/Set altitude TBD
const char HELLO                    = 'H'; // Let shutter know we're here
const char WATCHDOG_INTERVAL        = 'I'; // Tell shutter when to trigger the watchdog for communication loss with rotator
const char VOLTS_SHUTTER            = 'K'; // Get volts and set cutoff voltage (close if bellow)
const char SHUTTER_PING                 = 'L'; // Shutter ping, uses to reset watchdog timer.
const char STATE_SHUTTER            = 'M'; // Get shutter state
const char OPEN_SHUTTER             = 'O'; // Open the shutter
const char POSITION_SHUTTER		    = 'P'; // Get step position
const char SHUTTER_PANID            = 'Q'; // get and set the XBEE PAN ID
const char SPEED_SHUTTER            = 'R'; // Get/Set step rate (speed)
const char STEPSPER_SHUTTER         = 'T'; // Get/Set steps per stroke
const char VERSION_SHUTTER          = 'V'; // Get version string
const char REVERSED_SHUTTER         = 'Y'; // Get/Set stepper reversed status
#endif // STANDALONE
*/

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
void PingShutter();
#endif // STANDALONE
#ifdef USE_ETHERNET
void ReceiveNetwork(EthernetClient);
#endif // USE_ETHERNET
void ReceiveComputer();
void ProcessCommand();
void ReceiveWireless();
void ProcessWireless();

void setup()
{
#if defined(ARDUINO_ARCH_RP2040)
    core0Ready = false;
#endif
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
    DBPrintln("========== RTI-Zome controller booting ==========");
#endif
#ifdef USE_ETHERNET
    getMacAddress(MAC_Address, uidBuffer);
#ifdef DEBUG
    DBPrintln("MAC : " + String(MAC_Address[0], HEX) + String(":") +
                    String(MAC_Address[1], HEX) + String(":") +
                    String(MAC_Address[2], HEX) + String(":") +
                    String(MAC_Address[3], HEX) + String(":") +
                    String(MAC_Address[4], HEX) + String(":") +
                    String(MAC_Address[5], HEX) );
#endif
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

#if defined(ARDUINO_SAM_DUE)
    noInterrupts();
    attachInterrupt(digitalPinToInterrupt(HOME_PIN), homeIntHandler, FALLING);
    attachInterrupt(digitalPinToInterrupt(RAIN_SENSOR_PIN), rainIntHandler, CHANGE);
    attachInterrupt(digitalPinToInterrupt(BUTTON_CW), buttonHandler, CHANGE);
    attachInterrupt(digitalPinToInterrupt(BUTTON_CCW), buttonHandler, CHANGE);
    interrupts();
#endif

#ifdef USE_ETHERNET
    configureEthernet();
#endif // USE_ETHERNET
#ifdef DEBUG
    Computer.println("Online");
#endif
#if defined(ARDUINO_ARCH_RP2040)
    core0Ready = true;
    DBPrintln("========== Core 0 ready ==========");
#endif
}

#if defined(ARDUINO_ARCH_RP2040)
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
#endif

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
#if defined(ARDUINO_SAM_DUE)
    Rotator->Run();
#endif
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

#if defined(ARDUINO_ARCH_RP2040)
void loop1()
{   // all stepper motor code runs on core 1
    Rotator->Run();
}
#endif

#ifdef USE_ETHERNET
void configureEthernet()
{
    DBPrintln("========== Configureing Ethernet ==========");
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

    DBPrintln("========== Init Ethernet ==========");
    resetChip(ETHERNET_RESET);
    // network configuration
    nbEthernetClient = 0;
    Ethernet.init(ETHERNET_CS);

    DBPrintln("========== Setting IP config ==========");
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

    DBPrintln("========== Checking hardware status ==========");
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
#ifdef USE_ALPACA
    AlpacaServer.startServer();
#endif // USE_ALPACA
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
    Wireless.print("+++");
    delay(1100);
    ShutterWatchdog.reset();
    DBPrintln("Xbee +++ sent");
}

inline void ConfigXBee()
{

    DBPrintln("Sending ");
    if ( configStep == PANID_STEP) {
        String ATCmd = "ATID" + String(Rotator->GetPANID());
        DBPrintln(ATCmd);
        Wireless.println(ATCmd);
        Wireless.flush();
        configStep++;
    }
    else {
        DBPrintln(ATString[configStep]);
        Wireless.println(ATString[configStep]);
        Wireless.flush();
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
    DBPrintln("Sending hello");
    Wireless.print(String(HELLO) + "#");
    ReceiveWireless();
    SentHello = true;
}

void requestShutterData()
{
        Wireless.print(String(STATE_SHUTTER) + "#");
        ReceiveWireless();

        Wireless.print(String(VERSION_SHUTTER) + "#");
        ReceiveWireless();

        Wireless.print(String(REVERSED_SHUTTER) + "#");
        ReceiveWireless();

        Wireless.print(String(STEPSPER_SHUTTER) + "#");
        ReceiveWireless();

        Wireless.print(String(SPEED_SHUTTER) + "#");
        ReceiveWireless();

        Wireless.print(String(ACCELERATION_SHUTTER) + "#");
        ReceiveWireless();

        Wireless.print(String(VOLTS_SHUTTER) + "#");
        ReceiveWireless();

        Wireless.print(String(SHUTTER_PANID) + "#");
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
    if(bIsRaining != Rotator->GetRainStatus()) { // was there a state change ?
        bIsRaining = Rotator->GetRainStatus();
#ifndef STANDALONE
        Wireless.print(String(RAIN_SHUTTER) + String(bIsRaining ? "1" : "0") + "#");
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
        Wireless.print(String(RAIN_SHUTTER) + String(bIsRaining ? "1" : "0") + "#");
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
    if(PingTimer.elapsed() >= pingInterval) {
        Wireless.print(String(SHUTTER_PING) + "#");
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
            wirelessMessage = sTmpString;
            Wireless.print(wirelessMessage + "#");
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
            Wireless.print(sTmpString + "#");
            ReceiveWireless();
            DBPrintln("trying to reconfigure radio");
            resetChip(XBEE_RESET);
            break;

        case PANID:
            sTmpString = String(PANID);
            if (hasValue) {
                RemoteShutter.panid = "0000";
                wirelessMessage = String(SHUTTER_PANID) + value;
                Wireless.print(wirelessMessage + "#");
                setPANID(value); // shutter XBee should be doing the same thing
            }
            serialMessage = sTmpString + String(Rotator->GetPANID());
            break;

        case SHUTTER_PANID:
            wirelessMessage = String(SHUTTER_PANID);
            Wireless.print(wirelessMessage + "#");
            ReceiveWireless();
            serialMessage = String(SHUTTER_PANID) + RemoteShutter.panid ;
            break;


        case SHUTTER_PING:
            wirelessMessage = String(SHUTTER_PING);
            Wireless.print(wirelessMessage + "#");
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
            Wireless.print(wirelessMessage + "#");
            ReceiveWireless();
            serialMessage = sTmpString + RemoteShutter.acceleration;
            break;

        case CLOSE_SHUTTER:
            sTmpString = String(CLOSE_SHUTTER);
            Wireless.print(sTmpString+ "#");
            ReceiveWireless();
            serialMessage = sTmpString;
            break;

        case SHUTTER_RESTORE_MOTOR_DEFAULT :
            sTmpString = String(SHUTTER_RESTORE_MOTOR_DEFAULT);
            Wireless.print(sTmpString+ "#");
            ReceiveWireless();
            Wireless.print(String(SPEED_SHUTTER)+ "#");
            ReceiveWireless();
            Wireless.print(String(ACCELERATION_SHUTTER)+ "#");
            ReceiveWireless();
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
//          Wireless.print(wirelessMessage + "#");
//          ReceiveWireless();
//          serialMessage = sTmpString + RemoteShutter.position;
//          break;

        case OPEN_SHUTTER:
                sTmpString = String(OPEN_SHUTTER);
                Wireless.print(sTmpString + "#");
                ReceiveWireless();
                serialMessage = sTmpString + RemoteShutter.lowVoltStateOrRaining;
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
            Wireless.print(wirelessMessage + "#");
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
            Wireless.print(wirelessMessage + "#");
            ReceiveWireless();
            serialMessage = sTmpString + RemoteShutter.speed;
            break;

        case STATE_SHUTTER:
            sTmpString = String(STATE_SHUTTER);
            Wireless.print(sTmpString + "#");
            ReceiveWireless();
            serialMessage = sTmpString + RemoteShutter.state;
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
            Wireless.print(wirelessMessage + "#");
            ReceiveWireless();
            serialMessage = sTmpString + RemoteShutter.stepsPerStroke;
            break;

        case VERSION_SHUTTER:
            sTmpString = String(VERSION_SHUTTER);
            Wireless.print(sTmpString + "#");
            ReceiveWireless();
            serialMessage = sTmpString + RemoteShutter.version;
            break;

        case VOLTS_SHUTTER:
            sTmpString = String(VOLTS_SHUTTER);
            wirelessMessage = sTmpString;
            if (hasValue)
                wirelessMessage += String(value);

            Wireless.print(wirelessMessage + "#");
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
            Wireless.print(wirelessMessage + "#");
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
                Computer.print(serialMessage + "#");
            }
#ifdef USE_ETHERNET
        else if(domeClient.connected()) {
                DBPrintln("Network serialMessage = " + serialMessage);
                domeClient.print(serialMessage + "#");
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
                    DBPrintln("[ReceiveWireless] XBee timeout");
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
            Wireless.print(String(RAIN_SHUTTER) + String(bIsRaining ? "1" : "0") + "#");
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
            if (hasValue)
                RemoteShutter.volts = value;
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




