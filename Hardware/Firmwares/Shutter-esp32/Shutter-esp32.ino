//
// RTI-Zone Dome Shutter firmware.
// for ESP32
//

// Uncomment #define DEBUG to enable printing debug messages on serial port defined as DebugPort

#include "Arduino.h"
#include <atomic>
#define DEBUG   // enable debug to serial port defined as DebugPort

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

#define ERR_NO_DATA	-1

String IpAddress2String(const IPAddress& ipAddress)
{
  return String(ipAddress[0]) + String(".") +
  		String(ipAddress[1]) + String(".") +
		String(ipAddress[2]) + String(".") +
		String(ipAddress[3]);
}

#include "dome_commands.h"
#include "ShutterClass.h"

#ifdef DEBUG
String serialBuffer;
#endif

#include <WiFi.h>
#define SHUTTER_PORT 2424
#define shutterWiFi WiFi
WIFIConfig wifiConfig;
WiFiClient shutterClient;
IPAddress gwIp;
bool configureWiFi();
bool initWiFi(IPAddress ip, String sSSID, String sPassword);
void ReceiveWiFi(WiFiClient client);
void ProcessWifi();
bool rotatorReconnect(IPAddress ip);
String wifiBuffer = "";
std::atomic<bool> bWiFiOk;
std::atomic<bool> bNeedReconnect;
std::atomic<bool> isRaining;
std::atomic<bool> needFirstPing;
StopWatch watchdogTimer;
StopWatch reconnectTimer;
ShutterClass *Shutter = nullptr;

void MotorTask(void *);
void IRAM_ATTR handleClosedInterrupt();
void IRAM_ATTR handleOpenInterrupt();
void IRAM_ATTR handleButtons();
void setup()
{
	bNeedReconnect = false;
	isRaining = false;
	needFirstPing = true;
	bWiFiOk = false;
#ifdef DEBUG
	DebugPort.begin(115200);
	delay(1000);
	DBPrintln("========== RTI-Zone Shutter controller booting ==========");
#endif
	Shutter = new ShutterClass();
	watchdogTimer.reset();
	reconnectTimer.reset();
	Shutter->motorStop();
	Shutter->EnableMotor(false);
	disableCore0WDT();
	disableCore1WDT();
	xTaskCreatePinnedToCore(MotorTask, "MotorTask", 10000, NULL, 1, NULL,  0);

	WiFi.mode(WIFI_STA);
	WiFi.setHostname("RTI-Shutter");
	if(configureWiFi())
		needFirstPing = true;
	DBPrintln("========== Ready ==========");
}


void loop()
{
	// first check if we're connected to the WiFi AP of the rotator
	if(!bWiFiOk) {
		if(configureWiFi())
			needFirstPing = true;
		else {
			taskYIELD();
			delay(250);
			return;
		}
	}

	// Check if we lost connection or didn't connect and need to reconnect (client TCP connection, not WiFi)
	if((watchdogTimer.elapsed() >= Shutter->getWatchdogInterval()) || (bNeedReconnect && reconnectTimer.elapsed() > 15000)) {
		DBPrintln("Shutter->getWatchdogInterval() : " + String(Shutter->getWatchdogInterval()));
		DBPrintln("watchdogTimer.elapsed() : " + String(watchdogTimer.elapsed()));
		DBPrintln("bNeedReconnect : " + String(bNeedReconnect?"Yes":"No"));
		DBPrintln("reconnectTimer.elapsed() : " + String(reconnectTimer.elapsed()));

		watchdogTimer.reset();
		if(shutterClient.connected()) {
			shutterClient.stop();
		}
		if(bNeedReconnect) {
			if(rotatorReconnect(gwIp)) {}
				PingRotator();
		}
	}

	if(needFirstPing) {
		PingRotator();
	}

	if(Shutter->m_bButtonUsed)
		watchdogTimer.reset();

	CheckForCommands();
}

void MotorTask(void *)
{

	noInterrupts();
	attachInterrupt(digitalPinToInterrupt(OPENED_PIN), handleOpenInterrupt, FALLING);
	attachInterrupt(digitalPinToInterrupt(CLOSED_PIN), handleClosedInterrupt, FALLING);
	attachInterrupt(digitalPinToInterrupt(BUTTON_OPEN), handleButtons, CHANGE);
	attachInterrupt(digitalPinToInterrupt(BUTTON_CLOSE), handleButtons, CHANGE);
	interrupts();

	DBPrintln("========== Motor task ready ==========");
	for(;;) {
		Shutter->Run();
		taskYIELD();
	}
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
	int nTimeout = 0;

	bWiFiOk = false;
	gwIp = ip;
	gwIp[3] = 1;
	shutterClient.stop();
	shutterWiFi.config(ip,  gwIp, IPAddress(255,255,255,0));
	shutterWiFi.begin(sSSID.c_str(), sPassword.c_str());
	while (WiFi.status() != WL_CONNECTED) {
		DBPrintln("Waiting for WiFi");
		delay(1000);
		nTimeout++;
		if(nTimeout>15) { // 15 seconds should be plenty
			DBPrintln("========== Failed to start WiFi AP ==========");
			return false;
		}
    }

	bWiFiOk = true;
	DBPrintln("IP = " + IpAddress2String(WiFi.localIP()));

	if (!shutterClient.connect(gwIp, SHUTTER_PORT)) {
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

bool rotatorReconnect(IPAddress ip)
{
	if (!shutterClient.connect(ip, SHUTTER_PORT)) {
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
void IRAM_ATTR handleClosedInterrupt()
{
	Shutter->ClosedInterrupt();
}

void IRAM_ATTR handleOpenInterrupt()
{
	Shutter->OpenInterrupt();
}

void IRAM_ATTR handleButtons()
{
	Shutter->DoButtons();
}




void PingRotator()
{
	String wirelessMessage="";
	wirelessMessage = String(SHUTTER_PING) + "#";
	// make sure the rotator knows as soon as possible
	if (Shutter->GetVoltsAreLow()) {
		wirelessMessage += "L"; // low voltage detected
	}

	shutterClient.write(wirelessMessage.c_str());
	shutterClient.flush();

	// ask if it's raining
	wirelessMessage = String(RAIN_SHUTTER) + "#";
	shutterClient.write(wirelessMessage.c_str());
	shutterClient.flush();

	// say hello :)
	wirelessMessage = String(HELLO) + "#";
	shutterClient.write(wirelessMessage.c_str());

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
			sRotatorMessage = "V" + String(VERSION);
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
		sRotatorMessage+="#";
		DBPrintln(">>> Sending " + sRotatorMessage);
		shutterClient.write(sRotatorMessage.c_str());
		shutterClient.flush();
	}
}
