//
// RTI-Zone Dome Rotator firmware. Based on https://github.com/nexdome/Automation/tree/master/Firmwares
// As I contributed to the "old" 2,x firmware and was somewhat familiar with it I decided to reuse it and
// fix most of the known issues. I also added some feature related to XBee init and reset.
// This also is meant to run on an Arduino DUE as we put the AccelStepper run() call in an interrupt
//

// Debug printing, uncomment #define DEBUG to enable
#define DEBUG
#ifdef DEBUG
#define DebugPort Serial    // Programming port
#define DBPrint(x)   if(DebugPort) DebugPort.print(x)
#define DBPrintln(x) if(DebugPort) DebugPort.println(x)
#else
#define DBPrint(x)
#define DBPrintln(x)
#endif // DEBUG

#define ERR_NO_DATA	-1

String IpAddress2String(const IPAddress& ipAddress)
{
  return String(ipAddress[0]) + String(".") +
  		String(ipAddress[1]) + String(".") +
		String(ipAddress[2]) + String(".") +
		String(ipAddress[3]); 
}

#include "ShutterClass.h"

#ifdef DEBUG
String serialBuffer;
#endif
String wifiBuffer;

const String version = "2.645";

#include "dome_commands.h"

#include <WiFi.h>
#define SHUTTER_PORT 2424
WIFIConfig wifiConfig;
bool configureWiFi();
bool initWiFi(IPAddress ip, String sSSID, String sPassword);
void ReceiveWiFi(WiFiClient client);
void ProcessWifi();
WiFiMulti shutterWiFi;
WiFiClient shutterClient;
std::atomic<bool> bNeedReconnect = false;

ShutterClass *Shutter = NULL;

std::atomic<bool> isRaining = false;
std::atomic<bool> needFirstPing = true;
std::atomic<bool> core0Ready = false;
StopWatch watchdogTimer;
StopWatch reconnectTimer;


void setup()
{
	core0Ready = false;
	bNeedReconnect = false;
#ifdef DEBUG
	DebugPort.begin(115200);
	delay(1000);
	DBPrintln("========== RTI-Zone controller booting ==========");
#endif
	Shutter = new ShutterClass();
	watchdogTimer.reset();
	reconnectTimer.reset();
	Shutter->motorStop();
	Shutter->EnableMotor(false);
	WiFi.mode(WIFI_STA);
	WiFi.setHostname("RTI-Shutter");
	if(configureWiFi())
		needFirstPing = true;
	core0Ready = true;
	DBPrintln("========== Core 0 ready ==========");
}

void setup1()
{
	while(!core0Ready)
		delay(100);

	DBPrintln("========== Core 1 starting ==========");

	DBPrintln("========== Core 1 Attaching interrupt handler ==========");
	noInterrupts();
	attachInterrupt(digitalPinToInterrupt(OPENED_PIN), handleOpenInterrupt, FALLING);
	attachInterrupt(digitalPinToInterrupt(CLOSED_PIN), handleClosedInterrupt, FALLING);
	attachInterrupt(digitalPinToInterrupt(BUTTON_OPEN), handleButtons, CHANGE);
	attachInterrupt(digitalPinToInterrupt(BUTTON_CLOSE), handleButtons, CHANGE);
	interrupts();


	DBPrintln("========== Core 1 ready ==========");
}

void loop()
{
	// Check if we lost connection or didn't connect and need to reconnect
	if((watchdogTimer.elapsed() >= Shutter->getWatchdogInterval()) || (bNeedReconnect && reconnectTimer.elapsed() > 15000)) {
		DBPrintln("Shutter->getWatchdogInterval() : " + String(Shutter->getWatchdogInterval()));
		DBPrintln("watchdogTimer.elapsed() : " + String(watchdogTimer.elapsed()));
		DBPrintln("bNeedReconnect : " + String(bNeedReconnect?"Yes":"No"));
		DBPrintln("reconnectTimer.elapsed() : " + String(reconnectTimer.elapsed()));

		watchdogTimer.reset();
		if(shutterClient.connected()) {
			shutterClient.stop();
		}
		if(configureWiFi())
			PingRotator();
	}

	if(needFirstPing) {
		PingRotator();
	}

	if(Shutter->m_bButtonUsed)
		watchdogTimer.reset();
	
	CheckForCommands();
}

//
// This loop does all the motor controls
//
void loop1()
{
	   // all stepper motor code runs on core 1
	Shutter->Run();
}

// WiFi connection to rotator
bool configureWiFi()
{
	DBPrintln("========== Configuring WiFi ==========");
	Shutter->getWiFiConfig(wifiConfig);

	return initWiFi(wifiConfig.ip,
			String(wifiConfig.sSSID),
			String(wifiConfig.sPassword));
}

bool initWiFi(IPAddress ip, String sSSID, String sPassword)
{
	shutterClient.stopAll();
	WiFi.config(ip);
	shutterWiFi.addAP(sSSID.c_str(), sPassword.c_str());
	if(shutterWiFi.run()!=WL_CONNECTED) {
		DBPrintln("========== Failed to start WiFi AP ==========");
		bNeedReconnect=true;
		reconnectTimer.reset();
		return false;
	}
	DBPrintln("IP = " + IpAddress2String(WiFi.localIP()));
	lwipPollingPeriod(3);	
	if (!shutterClient.connect(WiFi.gatewayIP(), SHUTTER_PORT)) {
		DBPrintln("connection failed");
		bNeedReconnect=true;
		shutterClient.stop();
		reconnectTimer.reset();
		return false;
	}
	shutterClient.setNoDelay(true);
	bNeedReconnect=false;
	return true;
}

// interrupt
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




void PingRotator()
{
	String wirelessMessage="";
	wirelessMessage = String(SHUTTER_PING);
	// make sure the rotator knows as soon as possible
	if (Shutter->GetVoltsAreLow()) {
		wirelessMessage += "L"; // low voltage detected
	}

	shutterClient.print(wirelessMessage + "#");
	shutterClient.flush();

	// ask if it's raining
	shutterClient.print( String(RAIN_SHUTTER) + "#");
	shutterClient.flush();

	// say hello :)
	shutterClient.print( String(HELLO) + "#");
	shutterClient.flush();
	needFirstPing = false;
}

void CheckForCommands()
{
	ReceiveWiFi(shutterClient);
}

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

void ProcessWifi()
{
	char command;
	bool hasValue = false;
	String value;
	String sRotatorMessage;

	DBPrintln("<<< Received: '" + wifiBuffer + "'");
	command = wifiBuffer.charAt(0);
	value = wifiBuffer.substring(1);
	if (value.length() > 0)
		hasValue = true;

	DBPrintln("<<< Command:" + String(command) + " Value:" + value);

	switch (command) {
		case ACCELERATION_SHUTTER:
			if (hasValue) {
				DBPrintln("Set acceleration to " + value);
				Shutter->SetAcceleration(value.toInt());
			}
			sRotatorMessage = String(ACCELERATION_SHUTTER) + String(Shutter->GetAcceleration());
			DBPrintln("Acceleration is " + String(Shutter->GetAcceleration()));
			break;

		case ABORT:
			DBPrintln("STOP!");
			Shutter->Abort();
			sRotatorMessage = String(ABORT);
			break;

		case CLOSE_SHUTTER:
			DBPrintln("Close shutter");
			if (Shutter->GetState() != CLOSED) {
				Shutter->Close();
			}
			sRotatorMessage = String(STATE_SHUTTER) + String(Shutter->GetState());
			break;

		case HELLO:
			DBPrintln("Rotator says hello!");
			sRotatorMessage = String(HELLO);
			DBPrintln("Sending hello back");
			break;

		case OPEN_SHUTTER:
			DBPrintln("Received Open Shutter Command");
			if (isRaining) {
				sRotatorMessage = "OR"; // (O)pen command (R)ain cancel
				DBPrintln("Raining");
			}
			else if (Shutter->GetVoltsAreLow()) {
				sRotatorMessage = "OL"; // (O)pen command (L)ow voltage cancel
				DBPrintln("Voltage Low");
			}
			else {
				sRotatorMessage = "O"; // (O)pen command
				if (Shutter->GetState() != OPEN)
					Shutter->Open();
			}

			break;

		case POSITION_SHUTTER:
			sRotatorMessage = String(POSITION_SHUTTER) + String(Shutter->GetPosition());
			DBPrintln(sRotatorMessage);
			break;

		case WATCHDOG_INTERVAL:
			if (hasValue) {
				Shutter->SetWatchdogInterval((unsigned long)value.toInt());
				DBPrintln("Watchdog interval set to " + value + " ms");
			}
			else {
				DBPrintln("Watchdog interval " + String(Shutter->getWatchdogInterval()) + " ms");
			}
			sRotatorMessage = String(WATCHDOG_INTERVAL) + String(Shutter->getWatchdogInterval());
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
			sRotatorMessage = String(REVERSED_SHUTTER) + String(Shutter->GetReversed());
			DBPrintln(sRotatorMessage);
			break;

		case SPEED_SHUTTER:
			if (hasValue) {
				DBPrintln("Set speed to " + value);
				if (value.toInt() > 0) Shutter->SetMaxSpeed(value.toInt());
			}
			sRotatorMessage = String(SPEED_SHUTTER) + String(Shutter->GetMaxSpeed());
			DBPrintln(sRotatorMessage);
			break;

		case STATE_SHUTTER:
			sRotatorMessage = String(STATE_SHUTTER) + String(Shutter->GetState());
			DBPrintln(sRotatorMessage);
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
			sRotatorMessage = String(STEPSPER_SHUTTER) + String(Shutter->GetStepsPerStroke());
			break;

		case VERSION_SHUTTER:
			sRotatorMessage = "V" + version;
			DBPrintln(sRotatorMessage);
			break;

		case VOLTS_SHUTTER:
			if (hasValue) {
				Shutter->SetVoltsFromString(value);
				DBPrintln("Set volts to " + value);
			}
			sRotatorMessage = "K" + Shutter->GetVoltString();
			DBPrintln(sRotatorMessage);
			break;

		case SHUTTER_PING:
			sRotatorMessage = String(SHUTTER_PING);
			// make sure the rotator knows as soon as possible
			if (Shutter->GetVoltsAreLow()) {
				sRotatorMessage += "L"; // low voltage detected
			}
			else if(isRaining) {
				sRotatorMessage += "R"; // Raining
			}

			DBPrintln("Got Ping");
			watchdogTimer.reset();
			break;

		case RESTORE_MOTOR_DEFAULT:
			DBPrintln("Restore default motor settings");
			Shutter->restoreDefaultMotorSettings();
			sRotatorMessage = String(RESTORE_MOTOR_DEFAULT);
			break;

		default:
			DBPrintln("Unknown command " + String(command));
			break;
	}

	if (sRotatorMessage.length() > 0 && shutterClient.connected()) {
		DBPrintln(">>> Sending " + sRotatorMessage);
		shutterClient.print(sRotatorMessage +"#");
		shutterClient.flush();
	}
}


