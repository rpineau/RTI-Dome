//
// RTI-Zone Dome Rotator firmware. 
//
//  Copyright © 2018 Rodolphe Pineau. All rights reserved.
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

StopWatch ResetInterruptWatchdog;
static const unsigned long resetInterruptInterval = 43200000; // 12 hours

const String version = "2.645";

#include "dome_commands.h"
/*
// available A B J N S U W X Z
const char ABORT				= 'a';
const char CLOSE_SHUTTER		= 'C'; // Close shutter
const char RESTORE_MOTOR_DEFAULT    = 'D'; // restore default values for motor controll.
const char ACCELERATION_SHUTTER = 'E'; // Get/Set stepper acceleration
const char RAIN_ROTATOR			= 'F'; // Rotator telling us if it's raining or not
// const char ELEVATION_SHUTTER	= 'G'; // Get/Set altitude
const char HELLO				= 'H'; // Let rotator know we're here
const char WATCHDOG_INTERVAL_SET	= 'I'; // Tell us how long between checks in seconds
const char VOLTS_SHUTTER		= 'K'; // Get volts and get/set cutoff
const char SHUTTER_PING				= 'L'; // use to reset watchdong timer.
const char STATE_SHUTTER		= 'M'; // Get shutter state
const char OPEN_SHUTTER			= 'O'; // Open the shutter
const char POSITION_SHUTTER		= 'P'; // Get step position
const char PANID                = 'Q'; // get and set the XBEE PAN ID
const char SPEED_SHUTTER		= 'R'; // Get/Set step rate (speed)
const char STEPSPER_SHUTTER		= 'T'; // Get/Set steps per stroke
const char VERSION_SHUTTER		= 'V'; // Get version string
const char INIT_XBEE				= 'x'; // force a ConfigXBee
const char REVERSED_SHUTTER		= 'Y'; // Get/Set stepper reversed status
*/

#if defined(XBEE_S1)
#define NB_AT_OK  17
// ATAC,CE0,ID4242,CH0C,MY1,DH0,DL0,RR6,RN2,PL4,AP0,SM0,BD3,WR,FR,CN
String ATString[18] = {"ATRE","ATWR","ATAC","ATCE0","","ATCH0C","ATMY1","ATDH0","ATDL0",
						"ATRR6","ATRN2","ATPL4","ATAP0","ATSM0","ATBD3","ATWR","ATFR","ATCN"};
#endif

#if defined(XBEE_S2C)
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
	noInterrupts();
	attachInterrupt(digitalPinToInterrupt(OPENED_PIN), handleOpenInterrupt, FALLING);
	attachInterrupt(digitalPinToInterrupt(CLOSED_PIN), handleClosedInterrupt, FALLING);
	attachInterrupt(digitalPinToInterrupt(BUTTON_OPEN), handleButtons, CHANGE);
	attachInterrupt(digitalPinToInterrupt(BUTTON_CLOSE), handleButtons, CHANGE);
	ResetInterruptWatchdog.reset();
	interrupts();
	// enable input buffers
	Shutter->bufferEnable(true);
	Shutter->motorStop();
	Shutter->EnableMotor(false);
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
	else if(watchdogTimer.elapsed() >= (Shutter->getWatchdogInterval()*3)) {
		// could be the case is the rotator was off for a whille
		watchdogTimer.reset();
		PingRotator();
		delay(1000);
	}

	if(needFirstPing && XbeeStarted) {
		PingRotator();
	}
	if(Shutter->m_bButtonUsed)
		watchdogTimer.reset();

	Shutter->Run();
	checkInterruptTimer();

}

// reset intterupt as they seem to stop working after a while
void checkInterruptTimer()
{
	if(ResetInterruptWatchdog.elapsed() > resetInterruptInterval ) {
		if(Shutter->GetState() == OPEN || Shutter->GetState() == CLOSED) { // reset interrupt only if not doing anything
			noInterrupts();
			detachInterrupt(digitalPinToInterrupt(OPENED_PIN));
			detachInterrupt(digitalPinToInterrupt(CLOSED_PIN));
			detachInterrupt(digitalPinToInterrupt(BUTTON_OPEN));
			detachInterrupt(digitalPinToInterrupt(BUTTON_CLOSE));
			// re-attach interrupts
			attachInterrupt(digitalPinToInterrupt(OPENED_PIN), handleOpenInterrupt, FALLING);
			attachInterrupt(digitalPinToInterrupt(CLOSED_PIN), handleClosedInterrupt, FALLING);
			attachInterrupt(digitalPinToInterrupt(BUTTON_OPEN), handleButtons, CHANGE);
			attachInterrupt(digitalPinToInterrupt(BUTTON_CLOSE), handleButtons, CHANGE);
			ResetInterruptWatchdog.reset();
			interrupts();
		}
	}
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
	Wireless.write("+++");
	delay(1100);
	watchdogTimer.reset();
}

inline void ConfigXBee(String result)
{

	DBPrint("Sending : ");
	if ( configStep == PANID_STEP) {
		String ATCmd = "ATID" + String(Shutter->GetPANID());
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
	wirelessMessage += "#";
	Wireless.write(wirelessMessage.c_str());
	// ask if it's raining
	wirelessMessage  =String(RAIN_SHUTTER) + "#";
	Wireless.write( wirelessMessage.c_str());

	// say hello :)
	wirelessMessage  =String(HELLO) + "#";
	Wireless.write( wirelessMessage.c_str());
	needFirstPing = false;
}

#ifdef DEBUG
void ReceiveSerial()
{
	char computerCharacter;
	if(DebugPort.available() < 1)
		return; // no data

	while(DebugPort.available() > 0 ) {
		computerCharacter = DebugPort.read();
		if (computerCharacter != ERR_NO_DATA) {
			if (computerCharacter == '\r' || computerCharacter == '\n' || computerCharacter == '#') {
				// End of command
				if (serialBuffer.length() > 0) {
					ProcessMessages(serialBuffer);
					serialBuffer = "";
					return; // we'll read the next command on the next loop.
				}
			}
			else {
				serialBuffer += String(computerCharacter);
			}
		}
	}
}
#endif

void ReceiveWireless()
{
	char character;
	// read as much as possible in one call to ReceiveWireless()
	while(Wireless.available() > 0) {
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
		case ACCELERATION_SHUTTER:
			if (hasValue) {
				DBPrintln("Set acceleration to " + value);
				Shutter->SetAcceleration(value.toInt());
			}
			wirelessMessage = String(ACCELERATION_SHUTTER) + String(Shutter->GetAcceleration());
			DBPrintln("Acceleration is " + String(Shutter->GetAcceleration()));
			break;

		case ABORT:
			DBPrintln("STOP!");
			Shutter->Abort();
			wirelessMessage = String(ABORT);
			break;

		case CLOSE_SHUTTER:
			DBPrintln("Close shutter");
			if (Shutter->GetState() != CLOSED) {
				Shutter->Close();
			}
			wirelessMessage = String(STATE_SHUTTER) + String(Shutter->GetState());
			break;

		case HELLO:
			DBPrintln("Rotator says hello!");
			wirelessMessage = String(HELLO);
			DBPrintln("Sending hello back");
			break;

		case OPEN_SHUTTER:
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

		case POSITION_SHUTTER:
			wirelessMessage = String(POSITION_SHUTTER) + String(Shutter->GetPosition());
			DBPrintln(wirelessMessage);
			break;

		case WATCHDOG_INTERVAL:
			if (hasValue) {
				Shutter->SetWatchdogInterval((unsigned long)value.toInt());
				DBPrintln("Watchdog interval set to " + value + " ms");
			}
			else {
				DBPrintln("Watchdog interval " + String(Shutter->getWatchdogInterval()) + " ms");
			}
			wirelessMessage = String(WATCHDOG_INTERVAL) + String(Shutter->getWatchdogInterval());
			break;

		case RAIN_SHUTTER:
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

		case REVERSED_SHUTTER:
			if (hasValue) {
				Shutter->SetReversed(value.equals("1"));
				DBPrintln("Set Reversed to " + value);
			}
			wirelessMessage = String(REVERSED_SHUTTER) + String(Shutter->GetReversed());
			DBPrintln(wirelessMessage);
			break;

		case SPEED_SHUTTER:
			if (hasValue) {
				DBPrintln("Set speed to " + value);
				if (value.toInt() > 0) Shutter->SetMaxSpeed(value.toInt());
			}
			wirelessMessage = String(SPEED_SHUTTER) + String(Shutter->GetMaxSpeed());
			DBPrintln(wirelessMessage);
			break;

		case STATE_SHUTTER:
			wirelessMessage = String(STATE_SHUTTER) + String(Shutter->GetState());
			DBPrintln(wirelessMessage);
			break;

		case STEPSPER_SHUTTER:
			if (hasValue) {
				if (value.toInt() > 0) {
					Shutter->SetStepsPerStroke(value.toInt());
				}
			}
			else {
				DBPrintln("Get Steps " + String(Shutter->GetStepsPerStroke()));
			}
			wirelessMessage = String(STEPSPER_SHUTTER) + String(Shutter->GetStepsPerStroke());
			break;

		case VERSION_SHUTTER:
			wirelessMessage = "V" + version;
			DBPrintln(wirelessMessage);
			break;

		case VOLTS_SHUTTER:
			if (hasValue) {
				Shutter->SetVoltsFromString(value);
				DBPrintln("Set volts to " + value);
			}
			wirelessMessage = "K" + Shutter->GetVoltString();
			DBPrintln(wirelessMessage);
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

		case PANID:
			if (hasValue) {
				wirelessMessage = String(PANID);
				setPANID(value);
			}
			else {
				wirelessMessage = String(PANID) + Shutter->GetPANID();
			}
			DBPrintln("PAN ID '" + String(Shutter->GetPANID()) + "'");
			break;


		default:
			DBPrintln("Unknown command " + String(command));
			break;
	}

	if (wirelessMessage.length() > 0) {
		DBPrintln(">>> Sending " + wirelessMessage);
		wirelessMessage += "#";
		Wireless.write(wirelessMessage.c_str());
	}
}


