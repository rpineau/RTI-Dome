//
// RTI-Zone Dome Rotator firmware. Based on https://github.com/nexdome/Automation/tree/master/Firmwares
// As I contributed to the "old" 2,x firmware and was somewhat falilier with it I decided to reuse it and
// fix most of the known issues. I also added some feature related to XBee init and reset.
// This also is meant to run on an Arduino DUE as we put he AccelStepper run() call in an interrupt
//


// The Xbee S1 were the original one used on the NexDome controller.
// I have since tested with a pair of S2C that are easier to find and
// fix the Xbee init command to make it work.
// Also the XBee3 model XB3-24Z8PT-J work as the S1
#define XBEE_S1
// #define XBEE_S2C

#define DebugPort Serial    // programming port
#define Wireless Serial1    // XBEE

#define ERR_NO_DATA	-1
#define USE_EXT_EEPROM

#include "ShutterClass.h"

#ifdef DEBUG
String serialBuffer;
#endif
String wirelessBuffer;


const String version = "2.645";

// available A J N S U W X Z
const char ABORT_CMD				= 'a';
const char VOLTSCLOSE_SHUTTER_CMD	= 'B';
const char CLOSE_SHUTTER_CMD		= 'C'; // Close shutter
const char RESTORE_MOTOR_DEFAULT    = 'D'; // restore default values for motor controll.
const char ACCELERATION_SHUTTER_CMD = 'E'; // Get/Set stepper acceleration
const char RAIN_ROTATOR_GET			= 'F'; // Rotator telling us if it's raining or not
// const char ELEVATION_SHUTTER_CMD	= 'G'; // Get/Set altitude
const char HELLO_CMD				= 'H'; // Let rotator know we're here
const char WATCHDOG_INTERVAL_SET	= 'I'; // Tell us how long between checks in seconds
const char VOLTS_SHUTTER_CMD		= 'K'; // Get volts and get/set cutoff
const char SHUTTER_PING				= 'L'; // use to reset watchdong timer.
const char STATE_SHUTTER_GET		= 'M'; // Get shutter state
const char OPEN_SHUTTER_CMD			= 'O'; // Open the shutter
const char POSITION_SHUTTER_GET		= 'P'; // Get step position
const char PANID_GET                = 'Q'; // get and set the XBEE PAN ID
const char SPEED_SHUTTER_CMD		= 'R'; // Get/Set step rate (speed)
const char STEPSPER_SHUTTER_CMD		= 'T'; // Get/Set steps per stroke
const char VERSION_SHUTTER_GET		= 'V'; // Get version string
const char INIT_XBEE				= 'x'; // force a ConfigXBee
const char REVERSED_SHUTTER_CMD		= 'Y'; // Get/Set stepper reversed status


#if defined(XBEE_S1)
#define NB_AT_OK  17
// ATAC,CE0,ID4242,CH0C,MY1,DH0,DL0,RR6,RN2,PL4,AP0,SM0,BD3,WR,FR,CN
String ATString[18] = {"ATRE","ATWR","ATAC","ATCE0","","ATCH0C","ATMY1","ATDH0","ATDL0",
                        "ATRR6","ATRN2","ATPL4","ATAP0","ATSM0","ATBD3","ATWR","ATFR","ATCN"};
#else if defined(XBEE_S2C)
#define NB_AT_OK  14
/// ATAC,CE1,ID4242,DH0,DLFFFF,PL4,AP0,SM0,BD3,WR,FR,CN
String ATString[18] = {"ATRE","ATWR","ATAC","ATCE0","","ATDH0","ATDL0","ATJV1",
                        "ATPL4","ATAP0","ATSM0","ATBD3","ATWR","ATFR","ATCN"};
#endif


// index in array above where the command is empty.
// This allows us to change the Pan ID and store it in the EEPROM/Flash
#define PANID_STEP 4

#define XBEE_RESET_PIN  8

ShutterClass *Shutter;

int configStep = 0;

bool XbeeStarted, isConfiguringWireless;
bool isRaining = false;
bool isResetingXbee = false;
int XbeeResets = 0;
bool needFirstPing = true;

void setup()
{
    digitalWrite(XBEE_RESET_PIN, 0);
    pinMode(XBEE_RESET_PIN, OUTPUT);

#ifdef DEBUG
	DebugPort.begin(115200);
#endif
	Wireless.begin(9600);
    XbeeStarted = false;
	XbeeResets = 0;
	isConfiguringWireless = false;
    Shutter = new ShutterClass();
	watchdogTimer.reset();
    Shutter->EnableMotor(false);
    attachInterrupt(digitalPinToInterrupt(OPENED_PIN), handleOpenInterrupt, FALLING);
    attachInterrupt(digitalPinToInterrupt(CLOSED_PIN), handleClosedInterrupt, FALLING);
    attachInterrupt(digitalPinToInterrupt(BUTTON_OPEN), handleButtons, CHANGE);
    attachInterrupt(digitalPinToInterrupt(BUTTON_CLOSE), handleButtons, CHANGE);
    // enable input buffers
    Shutter->bufferEnable(true);
    ResetXbee();
    needFirstPing = true;
}

void loop()
{
#ifdef DEBUG
    if(DebugPort) {
        if (DebugPort.available() > 0) {
            ReceiveSerial();
        }
    }
#endif

	if (Wireless.available() > 0)
		ReceiveWireless();

	if (!XbeeStarted) {
		if (!isConfiguringWireless) {
			StartWirelessConfig();
            needFirstPing = true;
		}
		else {
			XbeeStarted = true;
			wirelessBuffer = "";
			DBPrintln("Radio configured");
		}
	}

    // if we lost 3 pings and had no coms for that long.. reset XBee close if we've already reset the XBee
	if((watchdogTimer.elapsed() >= (Shutter->getWatchdogInterval()*3)) && (Shutter->GetState() != CLOSED) && (Shutter->GetState() != CLOSING)) {
            DBPrintln("watchdogTimer triggered");
            // lets try to recover
	        if(!isResetingXbee && XbeeResets == 0) {
	            XbeeResets++;
	            isResetingXbee = true;
                ResetXbee();
                isConfiguringWireless = false;
                XbeeStarted = false;
                configStep = 0;
                StartWirelessConfig();
	        }
	        else if (!isResetingXbee){
                // we lost communication with the rotator.. close everything.
                if (Shutter->GetState() != CLOSED && Shutter->GetState() != CLOSING) {
                    DBPrintln("watchdogTimer triggered.. closing");
                    DBPrintln("watchdogTimer.elapsed() = " + String(watchdogTimer.elapsed()));
                    DBPrintln("Shutter->getWatchdogInterval() = " + String(Shutter->getWatchdogInterval()));
                    Shutter->Close();
                    }
            }
		delay(1000);
	}

    if(needFirstPing && XbeeStarted) {
        PingRotator();
    }
	if(Shutter->m_bButtonUsed)
	    watchdogTimer.reset();

	Shutter->Run();
}

void handleClosedInterrupt()
{
    Shutter->ClosedInterrupt();
}

void handleOpenInterrupt()
{
    Shutter->OpenInterrupt();
}

void handleButtons()
{
    Shutter->DoButtons();
}


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
    DBPrint("Sending : ");
    if ( configStep == PANID_STEP) {
        String ATCmd = "ATID" + String(Shutter->GetPANID());
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
		Shutter->SaveToEEProm();
        DBPrintln("Xbee configuration finished");

        isResetingXbee = false;
		while(Wireless.available() > 0) {
			Wireless.read();
		}
	}
    delay(100);
}

void ResetXbee()
{
    DBPrintln("Resetting Xbee");
    digitalWrite(XBEE_RESET_PIN, 0);
    delay(250);
    digitalWrite(XBEE_RESET_PIN, 1);
}

void setPANID(String value)
{
    Shutter->setPANID(value);
    isConfiguringWireless = false;
    XbeeStarted = false;
    configStep = 0;
}

void PingRotator()
{
    String wirelessMessage="";
    wirelessMessage = String(SHUTTER_PING);
    // make sure the rotator knows as soon as possible
    if (Shutter->GetVoltsAreLow()) {
        wirelessMessage += "L"; // low voltage detected
    }

    Wireless.print(wirelessMessage + "#");
    // ask if it's raining
    Wireless.print( String(RAIN_ROTATOR_GET) + "#");

    // say hello :)
    Wireless.print( String(HELLO_CMD) + "#");
    needFirstPing = false;
}

#ifdef DEBUG
void ReceiveSerial()
{
	char character = DebugPort.read();

    if (character != ERR_NO_DATA) {
        if (character == '\r' || character == '\n') {
            // End of command
            if (serialBuffer.length() > 0) {
                ProcessMessages(serialBuffer);
                serialBuffer = "";
            }
        }
        else {
            serialBuffer += String(character);
        }
    }
}
#endif

void ReceiveWireless()
{
	char character;
	// read as much as possible in one call to ReceiveWireless()
	while(Wireless.available()) {
		character = Wireless.read();
		if (character != ERR_NO_DATA) {
			watchdogTimer.reset(); // communication are working
			needFirstPing = false; // if we're getting messages from the rotator we don't need to ping
			if (character == '\r' || character == '#') {
				if (wirelessBuffer.length() > 0) {
					if (isConfiguringWireless) {
						DBPrint("Configuring XBee");
						ConfigXBee(wirelessBuffer);
					}
					else {
						ProcessMessages(wirelessBuffer);
					}
					wirelessBuffer = "";
				}
			}
			else {
				wirelessBuffer += String(character);
			}
		}
	} // end while
}

void ProcessMessages(String buffer)
{
	String value, wirelessMessage="";
	char command;
    bool hasValue = false;

	if (buffer.equals("OK")) {
		DBPrint("Buffer == OK");
		return;
	}

	command = buffer.charAt(0);
	value = buffer.substring(1); // Payload if the command has data.

	if (value.length() > 0)
		hasValue = true;

	DBPrintln("<<< Command:" + String(command) + " Value:" + value);

	switch (command) {
		case ACCELERATION_SHUTTER_CMD:
			if (hasValue) {
				DBPrintln("Set acceleration to " + value);
				Shutter->SetAcceleration(value.toInt());
			}
			wirelessMessage = String(ACCELERATION_SHUTTER_CMD) + String(Shutter->GetAcceleration());
			DBPrintln("Acceleration is " + String(Shutter->GetAcceleration()));
			break;

		case ABORT_CMD:
			DBPrintln("STOP!");
			Shutter->motorStop();
			wirelessMessage = String(ABORT_CMD);
			break;

		case CLOSE_SHUTTER_CMD:
			DBPrintln("Close shutter");
			if (Shutter->GetState() != CLOSED) {
				Shutter->Close();
			}
			wirelessMessage = String(STATE_SHUTTER_GET) + String(Shutter->GetState());
			break;

		case HELLO_CMD:
			DBPrintln("Rotator says hello!");
			wirelessMessage = String(HELLO_CMD);
			DBPrintln("Sending hello back");
			break;

		case OPEN_SHUTTER_CMD:
			DBPrintln("Received Open Shutter Command");
			if (isRaining) {
				wirelessMessage = "OR"; // (O)pen command (R)ain cancel
				DBPrintln("Raining");
			}
			else if (Shutter->GetVoltsAreLow()) {
				wirelessMessage = "OL"; // (O)pen command (L)ow voltage cancel
				DBPrintln("Voltage Low");
			}
			else {
				wirelessMessage = "O"; // (O)pen command
				if (Shutter->GetState() != OPEN)
				    Shutter->Open();
			}

			break;

		case POSITION_SHUTTER_GET:
			wirelessMessage = String(POSITION_SHUTTER_GET) + String(Shutter->GetPosition());
			DBPrintln(wirelessMessage);
			break;

		case WATCHDOG_INTERVAL_SET:
			if (hasValue) {
				Shutter->SetWatchdogInterval((unsigned long)value.toInt());
				DBPrintln("Watchdog interval set to " + value + " ms");
			}
			else {
    			DBPrintln("Watchdog interval " + String(Shutter->getWatchdogInterval()) + " ms");
			}
			wirelessMessage = String(WATCHDOG_INTERVAL_SET) + String(Shutter->getWatchdogInterval());
			break;

		case RAIN_ROTATOR_GET:
		    if(hasValue) {
                if (value.equals("1")) {
                    if (!isRaining) {
                        if (Shutter->GetState() != CLOSED && Shutter->GetState() != CLOSING)
                            Shutter->Close();
                        isRaining = true;
                        DBPrintln("It's raining! (" + value + ")");
                    }
                }
                else {
                    isRaining = false;
                    DBPrintln("It's not raining");
                }
		    }
			break;

		case REVERSED_SHUTTER_CMD:
			if (hasValue) {
				Shutter->SetReversed(value.equals("1"));
				DBPrintln("Set Reversed to " + value);
			}
			wirelessMessage = String(REVERSED_SHUTTER_CMD) + String(Shutter->GetReversed());
			DBPrintln(wirelessMessage);
			break;

		case SPEED_SHUTTER_CMD:
			if (hasValue) {
				DBPrintln("Set speed to " + value);
				if (value.toInt() > 0) Shutter->SetMaxSpeed(value.toInt());
			}
			wirelessMessage = String(SPEED_SHUTTER_CMD) + String(Shutter->GetMaxSpeed());
			DBPrintln(wirelessMessage);
			break;

		case STATE_SHUTTER_GET:
			wirelessMessage = String(STATE_SHUTTER_GET) + String(Shutter->GetState());
			DBPrintln(wirelessMessage);
			break;

		case STEPSPER_SHUTTER_CMD:
			if (hasValue) {
				if (value.toInt() > 0) {
					Shutter->SetStepsPerStroke(value.toInt());
				}
			}
			else {
				DBPrintln("Get Steps " + String(Shutter->GetStepsPerStroke()));
			}
			wirelessMessage = String(STEPSPER_SHUTTER_CMD) + String(Shutter->GetStepsPerStroke());
			break;

		case VERSION_SHUTTER_GET:
			wirelessMessage = "V" + version;
			DBPrintln(wirelessMessage);
			break;

		case VOLTS_SHUTTER_CMD:
			if (hasValue) {
				Shutter->SetVoltsFromString(value);
				DBPrintln("Set volts to " + value);
			}
			wirelessMessage = "K" + Shutter->GetVoltString();
			DBPrintln(wirelessMessage);
			break;

		case VOLTSCLOSE_SHUTTER_CMD:
			if (hasValue) {
				DBPrintln("Close on low voltage value inn" + String(value));
				Shutter->SetVoltsClose(value.toInt());
			}
			wirelessMessage = String(VOLTSCLOSE_SHUTTER_CMD) + String(Shutter->GetVoltsClose());
			DBPrintln("Close on low voltage " + String(Shutter->GetVoltsClose()));
			break;

		case INIT_XBEE:
			isConfiguringWireless = false;
			XbeeStarted = false;
			configStep = 0;
			wirelessMessage = String(INIT_XBEE);
			break;

		case SHUTTER_PING:
			wirelessMessage = String(SHUTTER_PING);
            // make sure the rotator knows as soon as possible
            if (Shutter->GetVoltsAreLow()) {
                wirelessMessage += "L"; // low voltage detected
            }
            else if(isRaining) {
                wirelessMessage += "R"; // Raining
            }

			DBPrintln("Got Ping");
			watchdogTimer.reset();
			XbeeResets = 0;
			break;

        case RESTORE_MOTOR_DEFAULT:
			DBPrintln("Restore default motor settings");
            Shutter->restoreDefaultMotorSettings();
			wirelessMessage = String(RESTORE_MOTOR_DEFAULT);
            break;

        case PANID_GET:
			if (hasValue) {
				wirelessMessage = String(PANID_GET);
				setPANID(value);
			}
			else {
                wirelessMessage = String(PANID_GET) + Shutter->GetPANID();
            }
            DBPrintln("PAN ID '" + String(Shutter->GetPANID()) + "'");
			break;


		default:
			DBPrintln("Unknown command " + String(command));
			break;
	}

	if (wirelessMessage.length() > 0) {
		DBPrintln(">>> Sending " + wirelessMessage);
		Wireless.print(wirelessMessage +"#");
	}
}


