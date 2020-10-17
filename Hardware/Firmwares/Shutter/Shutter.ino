//
// RTI-Zone Dome Shutter firmware. Based on https://github.com/nexdome/Automation/tree/master/Firmwares
// As I contributed a lot to it and it's being deprecated, I'm now using it for myself.
//

// #define TEENY_3_5
#define TB6600
// #define ISD0X


#if defined __SAM3X8E__ // Arduino DUE
#define ARDUINO_DUE
// DUE
#define Computer Serial     // programing port
#define DebugPort Computer
#define Wireless Serial1    // XBEE
#else
// Leonardo
#define Computer Serial
#define DebugPort Computer
#define Wireless Serial1    //XBEE
#endif

#define ERR_NO_DATA	-1

#include "ShutterClass.h"


#ifdef DEBUG
String serialBuffer;
#endif
String wirelessBuffer;

const String version = "2.642";

const char ABORT_CMD				= 'a';
const char VOLTSCLOSE_SHUTTER_CMD	= 'B';
const char CLOSE_SHUTTER_CMD		= 'C'; // Close shutter
const char SHUTTER_RESTORE_MOTOR_DEFAULT= 'D'; // restore default values for motor controll.
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


// ATAC,CE0,ID4242,CH0C,MY1,DH0,DL0,RR6,RN2,PL4,AP0,SM0,BD3,WR,FR,CN
String ATString[18] = {"ATRE","ATWR","ATAC","ATCE0","","ATCH0C","ATMY1","ATDH0","ATDL0",
                        "ATRR6","ATRN2","ATPL4","ATAP0","ATSM0","ATBD3","ATWR","ATFR","ATCN"};

// index in array above where the command is empty.
// This allows us to change the Pan ID and store it in the EEPROM/Flash
#define PANID_STEP 4

#if defined ARDUINO_DUE // Arduino DUE
#define XBEE_RESET_PIN  8
#endif

ShutterClass Shutter;

int configStep = 0;

bool XbeeStarted, isConfiguringWireless;
bool isRaining = false;
bool isResetingXbee = false;
int XbeeResets = 0;

unsigned long voltUpdateInterval = 5000;

bool doFinalUpdate = false;
bool	bManualMode = false;

void setup()
{
#ifdef DEBUG
	Computer.begin(115200);
#endif
	Wireless.begin(9600);

    XbeeStarted = false;
	XbeeResets = 0;
	isConfiguringWireless = false;
#if defined ARDUINO_DUE // Arduino DUE
    pinMode(XBEE_RESET_PIN, OUTPUT);
    digitalWrite(XBEE_RESET_PIN, 1);
#endif
	watchdogTimer.reset();
    Shutter.EnableMotor(false);
}

void loop()
{
#ifdef DEBUG
	if (Computer.available() > 0) {
		ReceiveSerial();
	}
#endif
	if (Wireless.available() > 0)
		ReceiveWireless();

	if (!XbeeStarted) {
		if (!Shutter.isRadioConfigured() && !isConfiguringWireless) {
			StartWirelessConfig();
		}
		else if (Shutter.isRadioConfigured()) {
			XbeeStarted = true;
			wirelessBuffer = "";
			DBPrintln("Radio configured");
		}
	}


	if((watchdogTimer.elapsed() >= Shutter.getWatchdogInterval()) && (Shutter.GetState() != CLOSED) && (Shutter.GetState() != CLOSING)) {
            DBPrintln("watchdogTimer triggered");
            // lets try to recover
	        if(!isResetingXbee && XbeeResets == 0) {
	            XbeeResets++;
	            isResetingXbee = true;
#if defined ARDUINO_DUE // Arduino DUE
                ResetXbee();
#endif
                Shutter.setRadioConfigured(false);
                isConfiguringWireless = false;
                XbeeStarted = false;
                configStep = 0;
                StartWirelessConfig();
	        }
	        else if (!isResetingXbee){
                // we lost communication with the rotator.. close everything.
                if (Shutter.GetState() != CLOSED && Shutter.GetState() != CLOSING) {
                    DBPrintln("watchdogTimer triggered.. closing");
                    DBPrintln("watchdogTimer.elapsed() = " + String(watchdogTimer.elapsed()));
                    DBPrintln("Shutter.getWatchdogInterval() = " + String(Shutter.getWatchdogInterval()));
                    Shutter.Close();
                    }
            }
		delay(1000);
	}


	if(Shutter.DoButtons())
	    watchdogTimer.reset();

	Shutter.Run();

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
        String ATCmd = "ATID" + String(Shutter.GetPANID());
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
	if (configStep > 17) {
		isConfiguringWireless = false;
		Shutter.setRadioConfigured(true);
		XbeeStarted = true;
		Shutter.SaveToEEProm();
        DBPrintln("Xbee configuration finished");

        isResetingXbee = false;
		while(Wireless.available() > 0) {
			Wireless.read();
		}
	}
    delay(100);
}

#if defined ARDUINO_DUE // Arduino DUE
void ResetXbee()
{
    DBPrintln("Resetting Xbee");
    digitalWrite(XBEE_RESET_PIN, 0);
    delay(250);
    digitalWrite(XBEE_RESET_PIN, 1);
}
#endif

void setPANID(String value)
{
    Shutter.setPANID(value);
    Shutter.setRadioConfigured(false);
    isConfiguringWireless = false;
    XbeeStarted = false;
    configStep = 0;
}

#ifdef DEBUG
void ReceiveSerial()
{
	char character = Computer.read();

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
#ifndef ARDUINO_DUE
    stepper.run(); // we don't want the stepper to stop
#endif
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
				Shutter.SetAcceleration(value.toInt());
			}
			wirelessMessage = String(ACCELERATION_SHUTTER_CMD) + String(Shutter.GetAcceleration());
			DBPrintln("Acceleration is " + String(Shutter.GetAcceleration()));
			break;

		case ABORT_CMD:
			DBPrintln("STOP!");
			Shutter.motorStop();
			wirelessMessage = String(ABORT_CMD);
			break;

		case CLOSE_SHUTTER_CMD:
			DBPrintln("Close shutter");
			if (Shutter.GetState() != CLOSED) {
				Shutter.Close();
			}
			wirelessMessage = String(STATE_SHUTTER_GET) + String(Shutter.GetState());
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
			else if (Shutter.GetVoltsAreLow()) {
				wirelessMessage = "OL"; // (O)pen command (L)ow voltage cancel
				DBPrintln("Voltage Low");
			}
			else {
				if (Shutter.GetState() != OPEN) Shutter.Open();
			}

			break;

		case POSITION_SHUTTER_GET:
			wirelessMessage = String(POSITION_SHUTTER_GET) + String(Shutter.GetPosition());
			DBPrintln(wirelessMessage);
			break;

		case WATCHDOG_INTERVAL_SET:
			if (hasValue) {
				Shutter.SetWatchdogInterval((unsigned long)value.toInt());
				DBPrintln("Watchdog interval set to " + value + " ms");
			}
			else {
    			DBPrintln("Watchdog interval " + String(Shutter.getWatchdogInterval()) + " ms");
			}
			wirelessMessage = String(WATCHDOG_INTERVAL_SET) + String(Shutter.getWatchdogInterval());
			break;

		case RAIN_ROTATOR_GET:
		    if(hasValue) {
                if (value.equals("1")) {
                    if (!isRaining) {
                        if (Shutter.GetState() != CLOSED && Shutter.GetState() != CLOSING)
                            Shutter.Close();
                        isRaining = true;
                        DBPrintln("It's raining! (" + value + ")");
                    }
                }
                else {
                    isRaining = false;
                    DBPrintln("It's not raining");
                }
                wirelessMessage = String(RAIN_ROTATOR_GET);
		    }
			break;

		case REVERSED_SHUTTER_CMD:
			if (hasValue) {
				Shutter.SetReversed(value.equals("1"));
				DBPrintln("Set Reversed to " + value);
			}
			wirelessMessage = String(REVERSED_SHUTTER_CMD) + String(Shutter.GetReversed());
			DBPrintln(wirelessMessage);
			break;

		case SPEED_SHUTTER_CMD:
			if (hasValue) {
				DBPrintln("Set speed to " + value);
				if (value.toInt() > 0) Shutter.SetMaxSpeed(value.toInt());
			}
			wirelessMessage = String(SPEED_SHUTTER_CMD) + String(Shutter.GetMaxSpeed());
			DBPrintln(wirelessMessage);
			break;

		case STATE_SHUTTER_GET:
			wirelessMessage = String(STATE_SHUTTER_GET) + String(Shutter.GetState());
			DBPrintln(wirelessMessage);
			break;

		case STEPSPER_SHUTTER_CMD:
			if (hasValue) {
				if (value.toInt() > 0) {
					Shutter.SetStepsPerStroke(value.toInt());
				}
			}
			else {
				DBPrintln("Get Steps " + String(Shutter.GetStepsPerStroke()));
			}
			wirelessMessage = String(STEPSPER_SHUTTER_CMD) + String(Shutter.GetStepsPerStroke());
			break;

		case VERSION_SHUTTER_GET:
			wirelessMessage = "V" + version;
			DBPrintln(wirelessMessage);
			break;

		case VOLTS_SHUTTER_CMD:
			if (hasValue) {
				Shutter.SetVoltsFromString(value);
				DBPrintln("Set volts to " + value);
			}
			wirelessMessage = "K" + Shutter.GetVoltString();
			DBPrintln(wirelessMessage);
			break;

		case VOLTSCLOSE_SHUTTER_CMD:
			if (hasValue) {
				DBPrintln("Close on low voltage value inn" + String(value));
				Shutter.SetVoltsClose(value.toInt());
			}
			wirelessMessage = String(VOLTSCLOSE_SHUTTER_CMD) + String(Shutter.GetVoltsClose());
			DBPrintln("Close on low voltage " + String(Shutter.GetVoltsClose()));
			break;

		case INIT_XBEE:
			Shutter.setRadioConfigured(false);
			isConfiguringWireless = false;
			XbeeStarted = false;
			configStep = 0;
			wirelessMessage = String(INIT_XBEE);
			break;

		case SHUTTER_PING:
			wirelessMessage = String(SHUTTER_PING);
			DBPrintln("Got Ping");
			watchdogTimer.reset();
			XbeeResets = 0;
			break;

        case SHUTTER_RESTORE_MOTOR_DEFAULT:
			DBPrintln("Restore default motor settings");
            Shutter.restoreDefaultMotorSettings();
			wirelessMessage = String(SHUTTER_RESTORE_MOTOR_DEFAULT);
            break;

        case PANID_GET:
			if (hasValue) {
				wirelessMessage = String(PANID_GET) + value;
    			Wireless.print(wirelessMessage + "#");
				setPANID(value); // shutter XBee should be doing the same thing
			}
            DBPrintln("PAN ID '" + String(Shutter.GetPANID()) + "'");
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

