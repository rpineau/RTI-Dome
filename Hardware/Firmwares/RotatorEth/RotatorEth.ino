//
// RTI-Zone Dome Rotator firmware. Based on https://github.com/nexdome/Automation/tree/master/Firmwares
// As I contributed to the "old" 2,x firmware and was somewhat falilier with it I decided to reuse it and
// fix most of the known issues. I also added some feature related to XBee init and reset.
// This is meant to run on an Arduino DUE as we put he AccelStepper run() call in an interrupt
//

// Uncomment #define DEBUG in RotatorClass.h to enable printing debug messages in serial

// if uncommented, STANDALONE will disable all code related to the XBee and the shutter.
// This us useful for people who only want to automate the rotation.
// #define STANDALONE

// The Xbee S1 were the original one used on the NexDome controller.
// I have since tested with a pair of S2C that are easier to find and
// fix the Xbee init command to make it work.
// Also the XBee3 model XB3-24Z8PT-J works usning the same config as the S1
#define XBEE_S1
// #define XBEE_S2C

// Stepper controller connection type :
// #define CommonAnode     // EN, DIR, STEP active LOW like ISD02/04/08
#define CommonCathode   // EN, DIR, STEP active HIGH like TB6600

#define MAX_TIMEOUT 100
#define ERR_NO_DATA -1
#define OK  0

#define VERSION "2.645"

#define USE_EXT_EEPROM

// include and some defines for ethernet connection
#include <SPI.h>
#include <Ethernet.h>
#include "EtherMac.h"

#define Computer Serial2     // USB-Serial FTDI FT323RL
#define FTDI_RESET  23
#ifndef STANDALONE
#define Wireless Serial1    // Serial1 on pin 18/19 for XBEE
#endif
#define DebugPort Serial    // programing port

#include "RotatorClass.h"

#define ETHERNET_CS     52
#define ETHERNET_RESET  53
uint32_t uidBuffer[4];  // DUE unique ID
byte MAC_Address[6];    // Mac address, uses part of the unique ID

#define SERVER_PORT 2323
EthernetServer domeServer(SERVER_PORT);
EthernetClient domeClient;
int nbEthernetClient;

String computerBuffer;
String networkBuffer;


#ifndef STANDALONE
#define XBEE_RESET  8
#include "RemoteShutterClass.h"
RemoteShutterClass RemoteShutter;
String wirelessBuffer;
bool XbeeStarted, sentHello, isConfiguringWireless, gotHelloFromShutter;
int configStep = 0;
#endif


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
#else if defined(XBEE_S2C)
#define NB_AT_OK  13
/// ATAC,CE1,ID4242,DH0,DLFFFF,PL4,AP0,SM0,BD3,WR,FR,CN
String ATString[18] = {"ATRE","ATWR","ATAC","ATCE1","","ATDH0","ATDLFFFF",
                        "ATPL4","ATAP0","ATSM0","ATBD3","ATWR","ATFR","ATCN"};
#endif
// index in array above where the command is empty.
// This allows us to change the Pan ID and store it in the EEPROM/Flash
#define PANID_STEP 4

static const unsigned long pingInterval = 30000; // 30 seconds, can't be changed

// Once booting is done and XBee is ready, broadcast a hello message
// so a shutter knows you're around if it is already running. If not,
// the shutter will send a hello when it's booted up.
bool SentHello = false;

// Timer to periodically ping the shutter.
StopWatch PingTimer;
StopWatch ShutterWatchdog;

#endif
bool bShutterPresent = false;

// global variable for rain status
bool bIsRaining = false;

// global variable for the IP config and to check if we detect the ethernet card
bool ethernetPresent;
IPConfig ServerConfig;

// Rotator commands
const char ABORT_MOVE_CMD               = 'a'; // Tell everything to STOP!
const char ETH_RECONFIG                 = 'b'; // reconfigure ethernet
const char CALIBRATE_ROTATOR_CMD        = 'c'; // Calibrate the dome
const char RESTORE_MOTOR_DEFAULT        = 'd'; // restore default values for motor controll.
const char ACCELERATION_ROTATOR_CMD     = 'e'; // Get/Set stepper acceleration
const char ETH_MAC_ADDRESS              = 'f'; // get the MAC adress.
const char GOTO_ROTATOR_CMD             = 'g'; // Get/set dome azimuth
const char HOME_ROTATOR_CMD             = 'h'; // Home the dome
const char HOMEAZ_ROTATOR_CMD           = 'i'; // Get/Set home position
const char IP_ADDRESS                   = 'j'; // get/set the IP address
const char VOLTS_ROTATOR_CMD            = 'k'; // Get volts and get/set cutoff
const char PARKAZ_ROTATOR_CMD           = 'l'; // Get/Set park azimuth
const char SLEW_ROTATOR_GET             = 'm'; // Get Slewing status/direction
const char RAIN_ROTATOR_ACTION          = 'n'; // Get/Set action when rain sensor triggered none, home, park
const char IS_SHUTTER_PRESENT           = 'o'; // check if the shutter has responded to pings
const char IP_SUBNET                    = 'p'; // get/set the ip subnet
const char PANID_GET                    = 'q'; // get and set the XBEE PAN ID
const char SPEED_ROTATOR_CMD            = 'r'; // Get/Set step rate (speed)
const char SYNC_ROTATOR_CMD             = 's'; // Sync to telescope
const char STEPSPER_ROTATOR_CMD         = 't'; // GetSteps per rotation
const char IP_GATEWAY                   = 'u'; // get/set IP default gateway
const char VERSION_ROTATOR_GET          = 'v'; // Get Version string
const char IP_DHCP                      = 'w'; // get/set DHCP mode
                                        //'x' see bellow
const char REVERSED_ROTATOR_CMD         = 'y'; // Get/Set stepper reversed status
const char HOMESTATUS_ROTATOR_GET       = 'z'; // Get homed status

const char RAIN_SHUTTER_GET             = 'F'; // Get rain status (from client) or tell shutter it's raining (from Rotator)

#ifndef STANDALONE
const char INIT_XBEE                    = 'x'; // force a XBee reconfig

// Shutter commands
const char VOLTSCLOSE_SHUTTER_CMD       = 'B'; // Get/Set if shutter closes and rotator homes on shutter low voltage
const char CLOSE_SHUTTER_CMD            = 'C'; // Close shutter
const char SHUTTER_RESTORE_MOTOR_DEFAULT= 'D'; // restore default values for motor controll.
const char ACCELERATION_SHUTTER_CMD     = 'E'; // Get/Set stepper acceleration
                                       // 'F' see above
//const char ELEVATION_SHUTTER_CMD      = 'G'; // Get/Set altitude TBD
const char HELLO_CMD                    = 'H'; // Let shutter know we're here
const char WATCHDOG_INTERVAL_SET        = 'I'; // Tell shutter when to trigger the watchdog for communication loss with rotator
const char VOLTS_SHUTTER_CMD            = 'K'; // Get volts and get/set cutoff
const char SHUTTER_PING                 = 'L'; // use to reset watchdong timer.
const char STATE_SHUTTER_GET            = 'M'; // Get shutter state
const char OPEN_SHUTTER_CMD             = 'O'; // Open the shutter
const char SHUTTER_PANID_GET            = 'Q'; // get and set the XBEE PAN ID
const char SPEED_SHUTTER_CMD            = 'R'; // Get/Set step rate (speed)
const char STEPSPER_SHUTTER_CMD         = 'T'; // Get/Set steps per stroke
const char VERSION_SHUTTER_GET          = 'V'; // Get version string
const char REVERSED_SHUTTER_CMD         = 'Y'; // Get/Set stepper reversed status
#endif

// function prototypes
void configureEthernet();
bool initEthernet(bool bUseDHCP, IPAddress ip, IPAddress dns, IPAddress gateway, IPAddress subnet);
void checkForNewTCPClient(void);
void homeIntHandler(void);
void rainIntHandler(void);
void buttonHandler(void);
void resetChip(int);
void resetFTDI(int);
void StartWirelessConfig(void);
void ConfigXBee(String);
void setPANID(String);
void SendHello(void);
void requestShutterData(void);
void CheckForCommands(void);
void CheckForRain(void);
void PingShutter(void);
void ReceiveNetwork(EthernetClient);
void ReceiveComputer(void);
void ProcessCommand(bool);
int ReceiveWireless(void);
void ProcessWireless(void);

void setup()
{
    // set reset pins to output and low
    digitalWrite(XBEE_RESET, 0);
    pinMode(XBEE_RESET, OUTPUT);

    digitalWrite(FTDI_RESET, 0);
    pinMode(FTDI_RESET, OUTPUT);

    digitalWrite(ETHERNET_RESET, 0);
    pinMode(ETHERNET_RESET, OUTPUT);

    resetChip(XBEE_RESET);
    resetFTDI(FTDI_RESET);

#ifdef DEBUG
    DebugPort.begin(115200);
#endif
    getMacAddress(MAC_Address, uidBuffer);

    Computer.begin(115200);
#ifndef STANDALONE
    Wireless.begin(9600);
    PingTimer.reset();
    XbeeStarted = false;
    sentHello = false;
    isConfiguringWireless = false;
    gotHelloFromShutter = false;
#endif
    Rotator = new RotatorClass();
    Rotator->EnableMotor(false);
    attachInterrupt(digitalPinToInterrupt(HOME_PIN), homeIntHandler, FALLING);
    attachInterrupt(digitalPinToInterrupt(RAIN_SENSOR_PIN), rainIntHandler, CHANGE);
    attachInterrupt(digitalPinToInterrupt(BUTTON_CW), buttonHandler, CHANGE);
    attachInterrupt(digitalPinToInterrupt(BUTTON_CCW), buttonHandler, CHANGE);
    // enable input buffers on older prototype
    Rotator->bufferEnable(true);
    configureEthernet();
}

void loop()
{

    if(ethernetPresent)
        checkForNewTCPClient();

#ifndef STANDALONE
    if (!XbeeStarted) {
        if (!Rotator->isRadioConfigured() && !isConfiguringWireless) {
            DBPrintln("Xbee reconfiguring");
            StartWirelessConfig();
            DBPrintln("Rotator->bIsRadioIsConfigured : " + String(Rotator->isRadioConfigured()));
            DBPrintln("isConfiguringWireless : " + String(isConfiguringWireless));
        }
        else if (Rotator->isRadioConfigured()) {
            XbeeStarted = true;
            wirelessBuffer = "";
            DBPrintln("Radio configured");
            SendHello();
        }
    }
#endif
    Rotator->Run();
    CheckForCommands();
    CheckForRain();
#ifndef STANDALONE
    if(XbeeStarted) {
        if(!SentHello)
            SendHello();
        PingShutter();
        if(ShutterWatchdog.elapsed() > (pingInterval*3)) {
            bShutterPresent = false;
        }
        if(gotHelloFromShutter) {
            requestShutterData();
            gotHelloFromShutter = false;
        }
    }
#endif

}

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
        dhcpOk = Ethernet.begin(MAC_Address);
        if(!dhcpOk) {
            DBPrintln("DHCP Failed!");
            Ethernet.begin(MAC_Address, ip, dns, gateway, subnet);
        } else {

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
    delay(800);
    digitalWrite(nPin,1);
    delay(10);
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
}

inline void ConfigXBee(String result)
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
        Rotator->setRadioConfigured(true);
        XbeeStarted = true;
        Rotator->SaveToEEProm();
        DBPrintln("Xbee configuration finished");
        while(Wireless.available() > 0) {
            Wireless.read();
        }
        SentHello = false;
        gotHelloFromShutter = false;
    }
    delay(100);
}

void setPANID(String value)
{
    Rotator->setPANID(value);
    Rotator->setRadioConfigured(false);
    isConfiguringWireless = false;
    XbeeStarted = false;
    configStep = 0;
}

void SendHello()
{
    DBPrintln("Sending hello");
    Wireless.print(String(HELLO_CMD) + "#");
    ReceiveWireless();
    SentHello = true;
}

void requestShutterData()
{
        Wireless.print(String(STATE_SHUTTER_GET) + "#");
        ReceiveWireless();

        Wireless.print(String(VERSION_SHUTTER_GET) + "#");
        ReceiveWireless();

        Wireless.print(String(REVERSED_SHUTTER_CMD) + "#");
        ReceiveWireless();

        Wireless.print(String(STEPSPER_SHUTTER_CMD) + "#");
        ReceiveWireless();

        Wireless.print(String(SPEED_SHUTTER_CMD) + "#");
        ReceiveWireless();

        Wireless.print(String(ACCELERATION_SHUTTER_CMD) + "#");
        ReceiveWireless();

        Wireless.print(String(VOLTS_SHUTTER_CMD) + "#");
        ReceiveWireless();

        Wireless.print(String(VOLTSCLOSE_SHUTTER_CMD) + "#");
        ReceiveWireless();
}

#endif

void CheckForCommands()
{
    ReceiveComputer();

#ifndef STANDALONE
    if (Wireless.available() > 0) {
        ReceiveWireless();
    }
#endif
    if(ethernetPresent )
        ReceiveNetwork(domeClient);
}

void CheckForRain()
{

    if(bIsRaining != Rotator->GetRainStatus()) { // was there a state change ?
        bIsRaining = Rotator->GetRainStatus();
#ifndef STANDALONE
        Wireless.print(String(RAIN_SHUTTER_GET) + String(bIsRaining ? "1" : "0") + "#");
        ReceiveWireless();
#endif
    }
    if (bIsRaining) {
        if (Rotator->GetRainAction() == HOME)
            Rotator->StartHoming();

        if (Rotator->GetRainAction() == PARK)
            Rotator->GoToAzimuth(Rotator->GetParkAzimuth());
    }
}

#ifndef STANDALONE

void PingShutter()
{
    if(PingTimer.elapsed() >= pingInterval) {
        Wireless.print(String(SHUTTER_PING )+ "#");
        ReceiveWireless();
        PingTimer.reset();
        }
}
#endif

void ReceiveNetwork(EthernetClient client)
{
    char networkCharacter;

    if(!client.connected()) {
        return;
    }

    if(client.available() < 1)
        return; // no data

    networkCharacter = client.read();
    if (networkCharacter != ERR_NO_DATA) {
        if (networkCharacter == '\r' || networkCharacter == '\n' || networkCharacter == '#') {
            // End of message
            if (networkBuffer.length() > 0) {
                ProcessCommand(true);
                networkBuffer = "";
            }
        }
        else {
            networkBuffer += String(networkCharacter);
        }
    }

}

// All comms are terminated with '#' but the '\r' and '\n' are for XBee config
void ReceiveComputer()
{
    if(Computer.available() < 1)
        return; // no data

    char computerCharacter = Computer.read();
    if (computerCharacter != ERR_NO_DATA) {
        if (computerCharacter == '\r' || computerCharacter == '\n' || computerCharacter == '#') {
            // End of message
            if (computerBuffer.length() > 0) {
                ProcessCommand(false);
                computerBuffer = "";
            }
        }
        else {
            computerBuffer += String(computerCharacter);
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
#endif
    String serialMessage, sTmpString;
    bool hasValue = false;

    // Split the buffer into command char and value if present
    // Command character
    if(bFromNetwork) {
        command = networkBuffer.charAt(0);
        // Payload
        value = networkBuffer.substring(1);
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
#endif

    DBPrintln("\nProcessCommand");
    DBPrintln("Command = \"" + String(command) +"\"");
    DBPrintln("Value = \"" + String(value) +"\"");
    DBPrintln("bFromNetwork = \"" + String(bFromNetwork?"Yes":"No") +"\"");


    switch (command) {
        case ABORT_MOVE_CMD:
            sTmpString = String(ABORT_MOVE_CMD);
            serialMessage = sTmpString;
            Rotator->Stop();
#ifndef STANDALONE
            wirelessMessage = sTmpString;
            Wireless.print(wirelessMessage + "#");
            ReceiveWireless();
#endif
            break;

        case ACCELERATION_ROTATOR_CMD:
            if (hasValue) {
                Rotator->SetAcceleration(value.toInt());
            }
            serialMessage = String(ACCELERATION_ROTATOR_CMD) + String(Rotator->GetAcceleration());
            break;

        case CALIBRATE_ROTATOR_CMD:
            Rotator->StartCalibrating();
            serialMessage = String(CALIBRATE_ROTATOR_CMD);
            break;

        case GOTO_ROTATOR_CMD:
            if (hasValue) {
                fTmp = value.toFloat();
                if ((fTmp >= 0.0) && (fTmp <= 360.0)) {
                    Rotator->GoToAzimuth(fTmp);
                }
            }
            serialMessage = String(GOTO_ROTATOR_CMD) + String(Rotator->GetAzimuth());
            break;
#ifndef STANDALONE
        case HELLO_CMD:
            SendHello();
            serialMessage = String(HELLO_CMD);
            break;
#endif
        case HOME_ROTATOR_CMD:
            Rotator->StartHoming();
            serialMessage = String(HOME_ROTATOR_CMD);
            break;

        case HOMEAZ_ROTATOR_CMD:
            if (hasValue) {
                fTmp = value.toFloat();
                if ((fTmp >= 0) && (fTmp < 360))
                    Rotator->SetHomeAzimuth(fTmp);
            }
            serialMessage = String(HOMEAZ_ROTATOR_CMD) + String(Rotator->GetHomeAzimuth());
            break;

        case HOMESTATUS_ROTATOR_GET:
            serialMessage = String(HOMESTATUS_ROTATOR_GET) + String(Rotator->GetHomeStatus());
            break;

        case PARKAZ_ROTATOR_CMD:
            sTmpString = String(PARKAZ_ROTATOR_CMD);
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

        case SPEED_ROTATOR_CMD:
            if (hasValue)
                Rotator->SetMaxSpeed(value.toInt());
            serialMessage = String(SPEED_ROTATOR_CMD) + String(Rotator->GetMaxSpeed());
            break;

        case REVERSED_ROTATOR_CMD:
            if (hasValue)
                Rotator->SetReversed(value.toInt());
            serialMessage = String(REVERSED_ROTATOR_CMD) + String(Rotator->GetReversed());
            break;

        case RESTORE_MOTOR_DEFAULT:
            Rotator->restoreDefaultMotorSettings();
            serialMessage = String(RESTORE_MOTOR_DEFAULT);
            break;

        case SLEW_ROTATOR_GET:
            serialMessage = String(SLEW_ROTATOR_GET) + String(Rotator->GetDirection());
            break;

        case STEPSPER_ROTATOR_CMD:
            if (hasValue)
                Rotator->SetStepsPerRotation(value.toInt());
            serialMessage = String(STEPSPER_ROTATOR_CMD) + String(Rotator->GetStepsPerRotation());
            break;

        case SYNC_ROTATOR_CMD:
            if (hasValue) {
                fTmp = value.toFloat();
                if (fTmp >= 0 && fTmp < 360) {
                    Rotator->SyncPosition(fTmp);
                    serialMessage = String(SYNC_ROTATOR_CMD) + String(Rotator->GetPosition());
                }
            }
            else {
                    serialMessage = String(SYNC_ROTATOR_CMD) + "E";
            }
            break;

        case VERSION_ROTATOR_GET:
            serialMessage = String(VERSION_ROTATOR_GET) + VERSION;
            break;

        case VOLTS_ROTATOR_CMD:
            if (hasValue) {
                Rotator->SetLowVoltageCutoff(value.toInt());
            }
            serialMessage = String(VOLTS_ROTATOR_CMD) + String(Rotator->GetVoltString());
            break;

        case RAIN_SHUTTER_GET:
            serialMessage = String(RAIN_SHUTTER_GET) + String(bIsRaining ? "1" : "0");
            break;

        case IS_SHUTTER_PRESENT:
            serialMessage = String(IS_SHUTTER_PRESENT) + String( bShutterPresent? "1" : "0");
            break;

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


#ifndef STANDALONE
        case INIT_XBEE:
            sTmpString = String(INIT_XBEE);
            Rotator->setRadioConfigured(false);
            isConfiguringWireless = false;
            XbeeStarted = false;
            configStep = 0;
            serialMessage = sTmpString;
            Wireless.print(sTmpString + "#");
            ReceiveWireless();
            DBPrintln("trying to reconfigure radio");
            break;

        case PANID_GET:
            sTmpString = String(PANID_GET);
            if (hasValue) {
                RemoteShutter.panid = "0000";
                wirelessMessage = SHUTTER_PANID_GET + value;
                Wireless.print(wirelessMessage + "#");
                setPANID(value); // shutter XBee should be doing the same thing
            }
            serialMessage = sTmpString + String(Rotator->GetPANID());
            break;


        case SHUTTER_PANID_GET:
            wirelessMessage = SHUTTER_PANID_GET;
            Wireless.print(wirelessMessage + "#");
            ReceiveWireless();
            serialMessage = SHUTTER_PANID_GET + RemoteShutter.panid ;
            break;

        case ACCELERATION_SHUTTER_CMD:
            sTmpString = String(ACCELERATION_SHUTTER_CMD);
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

        case CLOSE_SHUTTER_CMD:
            sTmpString = String(CLOSE_SHUTTER_CMD);
            Wireless.print(sTmpString+ "#");
            ReceiveWireless();
            serialMessage = sTmpString;
            break;

        case SHUTTER_RESTORE_MOTOR_DEFAULT :
            sTmpString = String(SHUTTER_RESTORE_MOTOR_DEFAULT);
            Wireless.print(sTmpString+ "#");
            ReceiveWireless();

            Wireless.print(String(SPEED_SHUTTER_CMD)+ "#");
            ReceiveWireless();

            Wireless.print(String(ACCELERATION_SHUTTER_CMD)+ "#");
            ReceiveWireless();
            serialMessage = sTmpString;
            break;

//      case ELEVATION_SHUTTER_CMD:
//          sTmpString = String(ELEVATION_SHUTTER_CMD);
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

        case OPEN_SHUTTER_CMD:
                sTmpString = String(OPEN_SHUTTER_CMD);
                Wireless.print(sTmpString + "#");
                ReceiveWireless();
                serialMessage = sTmpString + RemoteShutter.lowVoltStateOrRaining;
                break;

        case REVERSED_SHUTTER_CMD:
            sTmpString = String(REVERSED_SHUTTER_CMD);
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

        case SPEED_SHUTTER_CMD:
            sTmpString = String(SPEED_SHUTTER_CMD);
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

        case STATE_SHUTTER_GET:
            sTmpString = String(STATE_SHUTTER_GET);
            Wireless.print(sTmpString + "#");
            ReceiveWireless();
            serialMessage = sTmpString + RemoteShutter.state;
            break;

        case STEPSPER_SHUTTER_CMD:
            sTmpString = String(STEPSPER_SHUTTER_CMD);
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

        case VERSION_SHUTTER_GET:
            sTmpString = String(VERSION_SHUTTER_GET);
            Wireless.print(sTmpString + "#");
            ReceiveWireless();
            serialMessage = sTmpString + RemoteShutter.version;
            break;

        case VOLTS_SHUTTER_CMD:
            sTmpString = String(VOLTS_SHUTTER_CMD);
            wirelessMessage = sTmpString;
            if (hasValue)
                wirelessMessage += String(value);

            Wireless.print(wirelessMessage + "#");
            ReceiveWireless();
            serialMessage = sTmpString + RemoteShutter.volts;
            break;

        case VOLTSCLOSE_SHUTTER_CMD:
            sTmpString = String(VOLTSCLOSE_SHUTTER_CMD);
            if (value.length() > 0) {
                RemoteShutter.voltsClose = value;
                wirelessMessage = sTmpString+ value;
            }
            else {
                wirelessMessage = sTmpString;
            }
            Wireless.print(wirelessMessage + "#");
            ReceiveWireless();
            serialMessage = sTmpString + RemoteShutter.voltsClose;
            break;

        case WATCHDOG_INTERVAL_SET:
            sTmpString = String(WATCHDOG_INTERVAL_SET);
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
#endif

        default:
            serialMessage = "Unknown command:" + String(command);
            break;
    }


    // Send messages if they aren't empty.
    if (serialMessage.length() > 0) {
        if(!bFromNetwork) {
            Computer.print(serialMessage + "#");
            }
        else if(domeClient.connected()) {
                DBPrintln("Network serialMessage = " + serialMessage);
                domeClient.print(serialMessage + "#");
                domeClient.flush();
        }
    }
}




#ifndef STANDALONE
int ReceiveWireless()
{
    int timeout = 0;
    char wirelessCharacter;

    wirelessBuffer = "";
    if (isConfiguringWireless) {
        DBPrintln("[ReceiveWireless] isConfiguringWireless : " + String(isConfiguringWireless));
        // read the response
        do {
            while(Wireless.available() < 1) {
                delay(1);
                timeout++;
                if(timeout >= MAX_TIMEOUT) {
                    return ERR_NO_DATA;
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

        ConfigXBee(wirelessBuffer);
        return OK;
    }

    // wait for response
    timeout = 0;
    while(Wireless.available() < 1) {
        delay(5);   // give time to the shutter to reply
        timeout++;
        if(timeout >= MAX_TIMEOUT) {
            return ERR_NO_DATA;
            }
    }

    // read the response
    do {
        if(Wireless.available() > 0 ) {
            wirelessCharacter = Wireless.read();
            DBPrintln("[ReceiveWireless] received  : '" + String(wirelessCharacter) + "' ( 0x" + String(wirelessCharacter, HEX) + " )");
            if(wirelessCharacter != ERR_NO_DATA && wirelessCharacter!=0xFF && wirelessCharacter != '#') {
                wirelessBuffer += String(wirelessCharacter);
            }
        }
        delay(5);   // give time to the shutter to send data as a character takes about 1ms at 9600
    } while (wirelessCharacter != '#');

    if (wirelessBuffer.length() > 0) {
        ProcessWireless();
    }
    return OK;
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

    switch (command) {
        case ACCELERATION_SHUTTER_CMD:
            if (hasValue)
                RemoteShutter.acceleration = value;
            break;

        case HELLO_CMD:
            gotHelloFromShutter = true;
            bShutterPresent = true;
            break;

        case SPEED_SHUTTER_CMD:
            if (hasValue)
                RemoteShutter.speed = value;
            break;

        case RAIN_SHUTTER_GET:
            break;

        case REVERSED_SHUTTER_CMD:
            if (hasValue)
                RemoteShutter.reversed = value;
            break;

        case STATE_SHUTTER_GET:
            if (hasValue)
                RemoteShutter.state = value;
            break;

        case OPEN_SHUTTER_CMD:
            if (hasValue)
                RemoteShutter.lowVoltStateOrRaining = value;
            else
                RemoteShutter.lowVoltStateOrRaining = "";
            break;

        case STEPSPER_SHUTTER_CMD:
            if (hasValue)
                RemoteShutter.stepsPerStroke = value;
            break;

        case VERSION_SHUTTER_GET:
            if (hasValue)
                RemoteShutter.version = value;
            break;

        case VOLTS_SHUTTER_CMD:
            if (hasValue)
                RemoteShutter.volts = value;
            break;

        case VOLTSCLOSE_SHUTTER_CMD:
            if (hasValue)
                RemoteShutter.voltsClose = value;
            break;

        case WATCHDOG_INTERVAL_SET:
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

        case SHUTTER_PANID_GET:
            if (hasValue)
                 RemoteShutter.panid = value;
            break;

        default:
            break;
    }

}
#endif




