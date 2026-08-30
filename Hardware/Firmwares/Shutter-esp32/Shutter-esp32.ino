//
// RTI-Zone Dome Shutter firmware.
// for ESP32
//
//  Copyright © 2024 Rodolphe Pineau. All rights reserved.
//

//
// Board Settings (Tools menu):
// --------------------------------
// Board:            ESP32 Dev Module
// Flash Size:       4MB
// Partition Scheme: Minimal SPIFFS (1.9MB APP with OTA / 128KB SPIFFS)
// --------------------------------

// Uncomment #define DEBUG in config.h to enable printing debug messages on serial port defined as DebugPort

#include "Arduino.h"
#include <rtc_wdt.h>
#include <esp_task_wdt.h>
#include "config.h"

#define ERR_NO_DATA	-1

enum ConditionSensorStates {UNSAFE= 0, COND_SAFE, COND_UNKNOWN};


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
bool rotatorConnect(IPAddress ip);
String wifiBuffer = "";
volatile bool bWiFiOk = false;
volatile bool isBadCondition = false;
volatile bool needFirstPing = true;
StopWatch watchdogTimer;
ShutterClass *Shutter = nullptr;
TaskHandle_t MotorTaskHanle;
esp_task_wdt_config_t twdt_config =
    {
        .timeout_ms = 1000000,
        .idle_core_mask = 0,    // Bitmask of cores
        .trigger_panic = false,
    };

void MotorTask(void *);
void handleClosedInterrupt();
void handleOpenInterrupt();
void handleLowerClosedInterrupt();
void handleLowerOpenInterrupt();
void handleButtons();

void setup()
{
	isBadCondition = false;
	needFirstPing = true;
	bWiFiOk = false;
#ifdef DEBUG
	DebugPort.begin(115200);
	delay(1000);
	DBPrintln("========== RTI-Zone Shutter controller booting ==========");
#endif

	DBPrintln("========== Set WiFi mode ==========");
	shutterWiFi.mode(WIFI_STA);

	DBPrintln("========== Creating ShutterClass ==========");
	Shutter = new ShutterClass();

	DBPrintln("========== Disabling watchdog ==========");
	esp_task_wdt_deinit();
	esp_task_wdt_init(&twdt_config);
	esp_task_wdt_add(NULL);
	disableCore0WDT();
	disableCore1WDT();
	DBPrintln("========== Watchdog disabled ==========");

	watchdogTimer.reset();
	if(configureWiFi())
		needFirstPing = true;

	DBPrintln("========== Creating motor task ==========");
	xTaskCreatePinnedToCore(MotorTask, "MotorTask", 32768, NULL, 16, &MotorTaskHanle,  0);

	DBPrintln("========== Ready ==========");

}

bool firstLoop = true;

void loop()
{
	if(firstLoop) {
		firstLoop = false;
		DBPrintln("========== Shutter is Ready ==========");
		DBPrintln("Loop task priority : " + String(uxTaskPriorityGet(NULL)));
	}

	// first check if we're connected to the WiFi AP of the rotator
	if(!bWiFiOk) {
		DBPrintln("No Wifi, trying to reconfigure");
		shutterWiFi.disconnect();
		if(shutterWiFi.reconnect()) {
			rotatorConnect(wifiConfig.ip);
			needFirstPing = true;
		}
		else {
			DBPrintln("No Wifi, looping");
			taskYIELD();
			esp_task_wdt_reset();
		}
	}

	// Check if we lost connection and need to reconnect
	if(watchdogTimer.elapsed() >= Shutter->getWatchdogInterval()){
		DBPrintln("Shutter->getWatchdogInterval() : " + String(Shutter->getWatchdogInterval()));
		DBPrintln("watchdogTimer.elapsed() : " + String(watchdogTimer.elapsed()));
		shutterWiFi.disconnect();
		if(configureWiFi())
			needFirstPing = true;
		watchdogTimer.reset();
	}

	if(needFirstPing) {
		PingRotator();
	}

	if(Shutter->m_bButtonUsed)
		watchdogTimer.reset();

	CheckForCommands();

	taskYIELD();
	esp_task_wdt_reset();
}

void MotorTask(void *)
{
	const TickType_t xDelay = 50/ portTICK_PERIOD_MS; // 50ms task block to give time back

	DBPrintln("========== Motor task starting ==========");
	DBPrintln("========== Motor task Attaching interrupt handler ==========");

	attachInterrupt(OPENED_PIN, handleOpenInterrupt, FALLING);
	attachInterrupt(CLOSED_PIN, handleClosedInterrupt, FALLING);

	if(Shutter && Shutter->getDoubleShutterEnable()) {
		attachInterrupt(LOWER_OPENED_PIN, handleLowerOpenInterrupt, FALLING);
		attachInterrupt(LOWER_CLOSED_PIN, handleLowerClosedInterrupt, FALLING);
	}

	attachInterrupt(BUTTON_OPEN, handleButtons, FALLING);
	attachInterrupt(BUTTON_CLOSE, handleButtons, FALLING);
	esp_task_wdt_add(NULL);

	DBPrintln("========== Motor task ready ==========");
	for(;;) {
		Shutter->Run();
		taskYIELD();
		esp_task_wdt_reset();
		vTaskDelay(xDelay);
	}
}

// WiFi connection to rotator
bool configureWiFi()
{
	DBPrintln("========== Configuring WiFi ==========");
	Shutter->getWiFiConfig(wifiConfig);

	DBPrintln("========== WiFi configuration loaded ==========");

	return initWiFi(wifiConfig.ip,
			String(wifiConfig.sSSID),
			String(wifiConfig.sPassword));
}

bool initWiFi(IPAddress ip, String sSSID, String sPassword)
{
	int nTimeout = 0;
	DBPrintln("========== initWiFi ==========");
	bWiFiOk = false;
	DBPrintln("========== disconnect ==========");
	shutterWiFi.disconnect();

	DBPrintln("========== config ==========");
	DBPrintln("WiFi IP = " + IpAddress2String(ip));
	DBPrintln("WiFi gateway = " + IpAddress2String(gwIp));
	shutterWiFi.config(ip,  gwIp, IPAddress(255,255,255,0));

	DBPrintln("========== setHostname ==========");
	shutterWiFi.setHostname("RTI-Shutter");

	DBPrintln("========== begin ==========");
	DBPrintln("WiFi SSID = " + sSSID);
	DBPrintln("WiFi Password = " + sPassword);
	shutterWiFi.begin(sSSID.c_str(), sPassword.c_str());
	while (shutterWiFi.status() != WL_CONNECTED) {
		DBPrintln("Waiting for WiFi");
		vTaskDelay(1000 / portTICK_PERIOD_MS);
		nTimeout++;
		if(nTimeout>20) { // 20 seconds should be plenty, the rotator takes a few seconds to start
			DBPrintln("========== Failed to connect to Rotator ==========");
			return false;
		}
    }
	vTaskDelay(500 / portTICK_PERIOD_MS);
	return rotatorConnect(ip);
}

bool rotatorConnect(IPAddress ip)
{
	gwIp = ip;
	gwIp[3] = 1;
	DBPrintln("========== clientConnect ==========");

	shutterClient.stop();
	bWiFiOk = true;
	DBPrintln("IP = " + IpAddress2String(shutterWiFi.localIP()));

	if (!shutterClient.connect(gwIp, SHUTTER_PORT)) {
		DBPrintln("connection failed");
		shutterClient.stop();
		return false;
	}
	shutterClient.setNoDelay(true);
	return true;
}


// interrupt
void IRAM_ATTR handleClosedInterrupt()
{
	if(Shutter)
		Shutter->ClosedInterrupt();
}

void IRAM_ATTR handleOpenInterrupt()
{
	if(Shutter)
		Shutter->OpenInterrupt();
}

void IRAM_ATTR handleLowerClosedInterrupt()
{
	if(Shutter)
		Shutter->LowerClosedInterrupt();
}

void IRAM_ATTR handleLowerOpenInterrupt()
{
	if(Shutter)
		Shutter->LowerOpenInterrupt();
}

void IRAM_ATTR handleButtons()
{
	if(Shutter)
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
	wirelessMessage+= "#";

	shutterClient.write(wirelessMessage.c_str());

	// ask if condition are bad
	wirelessMessage = String(CONDITION_SHUTTER) + "#";
	shutterClient.write(wirelessMessage.c_str());

	// say hello :)
	wirelessMessage = String(HELLO) + "#";
	shutterClient.write(wirelessMessage.c_str());

	// report shutter state
	wirelessMessage = String(STATE_SHUTTER) + String(Shutter->GetState()) + "#";
	shutterClient.write(wirelessMessage.c_str());

	// report SSID
	wirelessMessage =  String(SHUTTER_SSID) + Shutter->getSSID() + "#";
	shutterClient.write(wirelessMessage.c_str());

	// report voltages
	wirelessMessage =  String(VOLTS_SHUTTER) + Shutter->GetVoltString() + "#";
	shutterClient.write(wirelessMessage.c_str());

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
#ifdef DEBUG
	DBPrintln("<<< Received: '" + wifiBuffer + "'");
#endif
	command = wifiBuffer.charAt(0);
	value = wifiBuffer.substring(1);
	if (value.length() > 0)
		hasValue = true;
#ifdef DEBUG
	DBPrintln("<<< Command:" + String(command) + " Value:" + value);
#endif
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
			if (isBadCondition) {
				sRotatorMessage = String(OPEN_SHUTTER)+"R"; // (O)pen command (R)ain cancel
				DBPrintln("Bad conditions");
			}
			else if (Shutter->GetVoltsAreLow()) {
				sRotatorMessage = String(OPEN_SHUTTER)+"L"; // (O)pen command (L)ow voltage cancel
				DBPrintln("Voltage Low");
			}
			else {
				sRotatorMessage = String(OPEN_SHUTTER); // (O)pen command
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

		case CONDITION_SHUTTER:
			if(hasValue) {
				if (value.equals(String(UNSAFE))) {
					if (!isBadCondition) {
						if (Shutter->GetState() != CLOSED && Shutter->GetState() != CLOSING)
							Shutter->Close();
						isBadCondition = true;
						DBPrintln("Bad conditions! (" + value + ")");
					}
				}
				else {
					isBadCondition = false;
					DBPrintln("Good conditions");
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
			else if(isBadCondition) {
				sRotatorMessage += "R"; // bad condition
			}
#ifdef DEBUG
			DBPrintln("Got Ping");
#endif
			watchdogTimer.reset();
			break;

		case RESTORE_MOTOR_DEFAULT:
			DBPrintln("Restore default motor settings");
			Shutter->restoreDefaultMotorSettings();
			sRotatorMessage = String(RESTORE_MOTOR_DEFAULT);
			break;

		case SHUTTER_SSID:
			sRotatorMessage = String(SHUTTER_SSID);
			if (hasValue) {
				Shutter->setSSID(value);
				if(!configureWiFi())
					return; // this will be picked by the wtachdog timer
			}
			sRotatorMessage += Shutter->getSSID();
			DBPrintln("SSID '" + String(Shutter->getSSID()) + "'");
			break;

		case RESET_ALL:
			Shutter->resetAlltoDefault(); // this reboots the ESP.
			break;

		case SHUTTER_ORDER:
			sRotatorMessage = String(SHUTTER_ORDER);
			if (hasValue) {
				Shutter->setOpenOrder(value.toInt()==1?true:false);
			}
			sRotatorMessage += Shutter->getOpenOrder();
			DBPrintln("SHUTTER_ORDER '" + String(Shutter->getOpenOrder()==TOP_FIRST?"Top first":"Bottom First") + "'");
			break;

		case DOUBLE_SHUTTER:
			sRotatorMessage = String(DOUBLE_SHUTTER);
			if (hasValue) {
				Shutter->setDoubleShutterEnable(value.toInt()==1?true:false);

				if(value.toInt()==1) {
					attachInterrupt(LOWER_OPENED_PIN, handleLowerOpenInterrupt, FALLING);
					attachInterrupt(LOWER_CLOSED_PIN, handleLowerClosedInterrupt, FALLING);
				} else {
					detachInterrupt(LOWER_OPENED_PIN);
					detachInterrupt(LOWER_CLOSED_PIN);
				}

			}
			sRotatorMessage += Shutter->getDoubleShutterEnable();
			DBPrintln("DOUBLE_SHUTTER '" + String(Shutter->getDoubleShutterEnable()?"True":"False") + "'");
			break;

		default:
			DBPrintln("Unknown command " + String(command));
			break;
	}

	if (sRotatorMessage.length() > 0 && shutterClient.connected()) {
		sRotatorMessage += "#";
#ifdef DEBUG
		DBPrintln(">>> Sending " + sRotatorMessage);
#endif
		shutterClient.write(sRotatorMessage.c_str());
	}
}
