//
// RTI-Zone Dome shutter firmware.
//
//  Copyright © 2024 Rodolphe Pineau. All rights reserved.
//
//

#include <atomic>
#include <Preferences.h>
#include <nvs_flash.h>
#include <AccelStepper.h>
#include "config.h"
#include "StopWatch.h"

typedef struct WIFICONFIG {
	IPAddress       ip;
	String 			sSSID;
	String			sPassword;
} WIFIConfig;


typedef struct ShutterConfiguration {
	unsigned long   stepsPerStroke;
	int             acceleration;
	int             maxSpeed;
	bool            reversed;
	int             cutoffVolts;
	unsigned long   watchdogInterval;
	bool            bHasDropShutter;
	bool            bTopShutterOpenFirst;
	WIFIConfig		wifiIpConfig;
} Configuration;


AccelStepper stepper(AccelStepper::DRIVER, STEPPER_STEP_PIN, STEPPER_DIRECTION_PIN);

// All possible Shutter state, including option got a dropout
enum ShutterStates { OPEN, CLOSED, OPENING, CLOSING, BOTTOM_OPEN, BOTTOM_CLOSED, BOTTOM_OPENING, BOTTOM_CLOSING, ERROR, FINISHING_OPEN, FINISHING_CLOSE };
std::atomic<ShutterStates> shutterState;

StopWatch buttonStopTimer;


class ShutterClass
{
public:
	// Constructor
	ShutterClass();

	bool        m_bWasRunning = false;

	// Motor functions
	float       PositionToAltitude(long);
	long        AltitudeToPosition(float);

	int         GetAcceleration();
	void        SetAcceleration(const int);

	int         GetMaxSpeed();
	void        SetMaxSpeed(const int);

	long        GetPosition();
	void        GotoPosition(const unsigned long);
	void        GotoAltitude(const float);
	void        MoveRelative(const long);
	float       GetElevation();

	bool        GetReversed();
	void        SetReversed(const bool);

	int         GetEndSwitchStatus();
	int         GetState();

	unsigned long	GetStepsPerStroke();
	void		SetStepsPerStroke(const unsigned long);

	bool        GetVoltsAreLow();
	String      GetVoltString();
	void        SetVoltsFromString(const String);

	unsigned long   getWatchdogInterval();
	void            SetWatchdogInterval(const unsigned long);

	// Move
	void		DoButtons();
	void		EnableMotor(const bool);
	void		Open();
	void		Close();
	void		Run();
	static void	motorStop();
	void		motorMoveTo(const long newPosition);
	void		motorMoveRelative(const long amount);

	// persistent data
	void		restoreDefaultMotorSettings();

	// interrupts
	void		ClosedInterrupt();
	void		OpenInterrupt();
	std::atomic<bool>     m_bButtonUsed;

	void    	Abort();

	String      getSSID();
	void        setSSID(String sSSID);
	void		getWiFiConfig(WIFIConfig &config);
private:

	Configuration   m_Config;
	Preferences 	m_preferences;

	float           m_fAdcConvert;
	int             m_nVolts;
	StopWatch       m_batteryCheckTimer;
	unsigned long   m_nBatteryCheckInterval;
	bool            m_bUserButtonStop;

	int             MeasureVoltage();

	void 			LoadConfig();
	bool			m_bDoSave;
};


ShutterClass::ShutterClass()
{
	int sw1, sw2;

	shutterState = ERROR;
	
	m_fAdcConvert = RES_MULT * (AD_REF / 4095.0f) * 100.0f;

	DBPrintln("configuring pins");

	// Input pins
	pinMode(CLOSED_PIN,             INPUT_PULLUP);
	pinMode(OPENED_PIN,             INPUT_PULLUP);
	pinMode(BUTTON_OPEN,            INPUT_PULLUP);
	pinMode(BUTTON_CLOSE,           INPUT_PULLUP);
	pinMode(VOLTAGE_MONITOR_PIN,    INPUT);

	// Ouput pins
	pinMode(STEPPER_STEP_PIN,       OUTPUT);
	pinMode(STEPPER_DIRECTION_PIN,  OUTPUT);
	pinMode(STEPPER_ENABLE_PIN,     OUTPUT);

	DBPrintln("Loading config");

	LoadConfig();

	DBPrintln("Configuring stepper");

	m_bDoSave = false;  // we just read the config, no need to resave all the value we're setting
	stepper.setEnablePin(STEPPER_ENABLE_PIN);
	SetAcceleration(m_Config.acceleration);
	SetMaxSpeed(m_Config.maxSpeed);
	// set pulse width
	stepper.setMinPulseWidth(MIN_PULSE_WIDTH); // 5uS to test. Default in the source seems to be set to 1 ...
	EnableMotor(false);

	// reset all timers
	m_nBatteryCheckInterval = BATTERY_CHECK_INTERVAL;
	m_batteryCheckTimer.reset();

	// read initial shutter state
	sw1 = digitalRead(CLOSED_PIN);
	sw2 = digitalRead(OPENED_PIN);

	shutterState = ERROR;
	if (sw1 == LOW && sw2 == HIGH)
		shutterState = CLOSED;
	else if (sw1 == HIGH && sw2 == LOW)
		shutterState = OPEN;

	m_bUserButtonStop=false;
	m_bButtonUsed = false;
	m_nVolts = MeasureVoltage();
	m_bDoSave = true;
}

void IRAM_ATTR ShutterClass::ClosedInterrupt()
{
	// debounce
	if (digitalRead(CLOSED_PIN) == LOW) {
		if(shutterState == CLOSING) {
			motorStop();
			shutterState = FINISHING_CLOSE;
		}
	}
}

void IRAM_ATTR ShutterClass::OpenInterrupt()
{
	// debounce
	if (digitalRead(OPENED_PIN) == LOW) {
		if(shutterState == OPENING) {
			motorStop();
			shutterState = FINISHING_OPEN;
		}
	}
}



void ShutterClass::LoadConfig()
{
	bool nvsInitDone = false;

	DBPrintln("LoadConfig");
	//  zero the structure so currently unused parts
	//  dont end up loaded with random garbage
	m_preferences.begin("RTI_Shutter", false);
	nvsInitDone = m_preferences.isKey("nvsInit");
	if(!nvsInitDone) {
		DBPrintln("Initializing NVS");
		m_preferences.end();
		nvs_flash_erase();
		nvs_flash_init();
		m_preferences.begin("RTI_Shutter", false);
		m_preferences.putBool("nvsInit", true);
	}

	m_Config.stepsPerStroke = m_preferences.getULong("stepsPerStroke",STEPS_DEFAULT);
	m_Config.acceleration = m_preferences.getInt("acceleration",ACCELERATION);
	m_Config.maxSpeed = m_preferences.getInt("maxSpeed",MAX_SPEED);
	m_Config.reversed = m_preferences.getBool("reversed", false);
	m_Config.cutoffVolts = m_preferences.getInt("cutoffVolts",1150);
	m_Config.watchdogInterval = m_preferences.getULong("watchdogInterval",DEFAULT_WATCHDOG_INTERVAL);
	m_Config.bHasDropShutter = m_preferences.getBool("hasDropShutter", false);
	m_Config.bTopShutterOpenFirst = m_preferences.getBool("topShutterOpenFirst", true); // this generaly the case.

	m_Config.wifiIpConfig.ip.fromString(m_preferences.getString("wifi_ip","172.31.255.2"));
	m_Config.wifiIpConfig.sSSID = m_preferences.getString("wifiSSID", "RTIShutter");
	m_Config.wifiIpConfig.sPassword = m_preferences.getString("wifiPassword", "RTIShutter");

	DBPrintln("m_Config.stepsPerStroke       : " + String(m_Config.stepsPerStroke));
	DBPrintln("m_Config.acceleration         : " + String(m_Config.acceleration));
	DBPrintln("m_Config.maxSpeed             : " + String(m_Config.maxSpeed));
	DBPrintln("m_Config.reversed             : " + String(m_Config.reversed?"Yes":"No"));
	DBPrintln("m_Config.cutoffVolts          : " + String(m_Config.cutoffVolts));
	DBPrintln("m_Config.watchdogInterval     : " + String(m_Config.watchdogInterval));
	DBPrintln("m_Config.bHasDropShutter      : " + String(m_Config.bHasDropShutter?"Yes":"No"));
	DBPrintln("m_Config.bTopShutterOpenFirst : " + String(m_Config.bTopShutterOpenFirst?"Yes":"No"));
	DBPrintln("wifiIpConfig.ip               : " + IpAddress2String(m_Config.wifiIpConfig.ip));
	DBPrintln("wifiIpConfig.sSSID            : " + String(m_Config.wifiIpConfig.sSSID));
	DBPrintln("wifiIpConfig.sPassword        : " + String(m_Config.wifiIpConfig.sPassword));

	if(m_Config.watchdogInterval > MAX_WATCHDOG_INTERVAL) {
		m_Config.watchdogInterval = MAX_WATCHDOG_INTERVAL;
		m_preferences.putULong("watchdogInterval", m_Config.watchdogInterval);
	}
	if(m_Config.watchdogInterval < MIN_WATCHDOG_INTERVAL) {
		m_Config.watchdogInterval = MIN_WATCHDOG_INTERVAL;
		m_preferences.putULong("watchdogInterval", m_Config.watchdogInterval);
	}
	m_preferences.end();
}


String ShutterClass::getSSID()
{
	return m_Config.wifiIpConfig.sSSID;
}

void ShutterClass::setSSID(String sSSID)
{
	m_Config.wifiIpConfig.sSSID = sSSID;
	m_preferences.begin("RTI_Shutter", false);
	m_preferences.putString("wifiSSID", sSSID);
	m_preferences.end();

}


void ShutterClass::getWiFiConfig(WIFIConfig &config)
{
	config.ip = m_Config.wifiIpConfig.ip;
	config.sSSID = m_Config.wifiIpConfig.sSSID;
	config.sPassword = m_Config.wifiIpConfig.sPassword;
}

float ShutterClass::PositionToAltitude(const long pos)
{
	float result = (float)pos;
	result = result / m_Config.stepsPerStroke * 90.0f;
	return result;
}

long ShutterClass::AltitudeToPosition(const float alt)
{
	long result;

	result = (long)(m_Config.stepsPerStroke * alt / 90.0f);
	return result;
}

int ShutterClass::GetAcceleration()
{
	return m_Config.acceleration;
}

void ShutterClass::SetAcceleration(const int accel)
{
	m_Config.acceleration = accel;
	stepper.setAcceleration(accel);
	if(m_bDoSave) {
		m_preferences.begin("RTI_Shutter", false);
		m_preferences.putInt("acceleration", accel);
		m_preferences.end();
	}
}

int ShutterClass::GetMaxSpeed()
{
	return stepper.maxSpeed();
}

void ShutterClass::SetMaxSpeed(const int speed)
{
	m_Config.maxSpeed = speed;
	stepper.setMaxSpeed(speed);
	if(m_bDoSave) {
		m_preferences.begin("RTI_Shutter", false);
		m_preferences.putInt("maxSpeed", speed);
		m_preferences.end();
	}
}

long ShutterClass::GetPosition()
{
	return stepper.currentPosition();
}

void ShutterClass::GotoPosition(const unsigned long newPos)
{
	uint64_t currentPos = stepper.currentPosition();
	bool doMove = false;

	// Check if this actually changes position, then move if necessary.
	if (newPos > currentPos) {
		DBPrintln("shutterState = OPENING");
		shutterState = OPENING;
		doMove = true;
	}
	else if (newPos < currentPos) {
		DBPrintln("shutterState = CLOSING");
		shutterState = CLOSING;
		doMove = true;
	}

	if (doMove) {
		motorMoveTo(newPos);
	}
}

void ShutterClass::GotoAltitude(const float newAlt)
{
	GotoPosition(AltitudeToPosition(newAlt));
}

void ShutterClass::MoveRelative(const long amount)
{
	motorMoveRelative(amount);
}

float ShutterClass::GetElevation()
{
	return PositionToAltitude(stepper.currentPosition());
}

bool ShutterClass::GetReversed()
{
	return m_Config.reversed;
}

void ShutterClass::SetReversed(const bool reversed)
{
	m_Config.reversed = reversed;
	stepper.setPinsInverted(reversed, reversed, reversed);
	if(m_bDoSave) {
		m_preferences.begin("RTI_Shutter", false);
		m_preferences.putBool("reversed", reversed);
		m_preferences.end();
	}
}

int ShutterClass::GetEndSwitchStatus()
{
	int result= ERROR;

	if (digitalRead(CLOSED_PIN) == LOW)
		result = CLOSED;

	if (digitalRead(OPENED_PIN) == LOW)
		result = OPEN;
	return result;
}

int ShutterClass::GetState()
{
	return shutterState;
}

unsigned long ShutterClass::GetStepsPerStroke()
{
	return m_Config.stepsPerStroke;
}

void ShutterClass::SetStepsPerStroke(const unsigned long newSteps)
{
	m_Config.stepsPerStroke = newSteps;
	if(m_bDoSave) {
		m_preferences.begin("RTI_Shutter", false);
		m_preferences.putULong("stepsPerRot", newSteps);
		m_preferences.end();
	}
}

void ShutterClass::restoreDefaultMotorSettings()
{
	SetMaxSpeed(MAX_SPEED);
	SetAcceleration(ACCELERATION);
	SetStepsPerStroke(STEPS_DEFAULT);
}


inline bool ShutterClass::GetVoltsAreLow()
{
	m_nVolts = MeasureVoltage();  // make sure we're using the current value
	bool low = (m_nVolts <= m_Config.cutoffVolts);
	return low;
}

String ShutterClass::GetVoltString()
{
	m_nVolts = MeasureVoltage();  // make sure we're reporting the current value
	return String(m_nVolts) + "," + String(m_Config.cutoffVolts);
}


void ShutterClass::SetVoltsFromString(const String value)
{
	m_Config.cutoffVolts = value.toInt();
	m_preferences.begin("RTI_Shutter", false);
	m_preferences.putInt("cutoffVolts", m_Config.cutoffVolts);
	m_preferences.end();
}

int ShutterClass::MeasureVoltage()
{
	int adc;
	float calc;

	adc = analogRead(VOLTAGE_MONITOR_PIN);
	calc = adc * m_fAdcConvert;
	if(calc - int(calc) >= 0.5)
		return int(ceil(calc));
	return int(calc);
}


unsigned long ShutterClass::getWatchdogInterval()
{
	return m_Config.watchdogInterval;
}

inline void ShutterClass::SetWatchdogInterval(const unsigned long newInterval)
{
	if(newInterval > MAX_WATCHDOG_INTERVAL)
		m_Config.watchdogInterval = MAX_WATCHDOG_INTERVAL;
	else    if(newInterval < MIN_WATCHDOG_INTERVAL)
		m_Config.watchdogInterval = MIN_WATCHDOG_INTERVAL;
	else
		m_Config.watchdogInterval = newInterval;

	m_preferences.begin("RTI_Shutter", false);
	m_preferences.putULong("watchdogInterval", m_Config.watchdogInterval);
	m_preferences.end();
}

// INPUTS
void IRAM_ATTR ShutterClass::DoButtons()
{
	int sw1, sw2, sw3, sw4;

	sw1 = digitalRead(BUTTON_OPEN);
	sw2 = digitalRead(BUTTON_CLOSE);

	sw3 = digitalRead(CLOSED_PIN);
	sw4 = digitalRead(OPENED_PIN);

	// shutter is between open and close and we want to open
	if(sw1 == LOW  && sw3 == HIGH && sw4 == HIGH ) {
		motorStop();
		shutterState = OPENING;
		MoveRelative(160000000L);
		m_bButtonUsed = true;
		m_bUserButtonStop = false;
		buttonStopTimer.reset();
	}
	// shutter is between open and close and we want to close
	else if(sw2 == LOW && sw3 == HIGH && sw4 == HIGH ) {
		motorStop();
		shutterState = CLOSING;
		MoveRelative(-160000000L);
		m_bButtonUsed = true;
		m_bUserButtonStop = false;
		buttonStopTimer.reset();
	}
	else if (sw1 == LOW && sw3 == LOW && sw4 == HIGH) { // button open pressed and we're closed
		shutterState = OPENING;
		MoveRelative(160000000L);
		m_bButtonUsed = true;
		m_bUserButtonStop = false;
		buttonStopTimer.reset();
	}
	else if (sw2 == LOW && sw3 == HIGH && sw4 == LOW) { // button close pressed and we're open
		shutterState = CLOSING;
		MoveRelative(-160000000L);
		m_bButtonUsed = true;
		m_bUserButtonStop = false;
		buttonStopTimer.reset();
	}
	else {
		buttonStopTimer.reset();
		motorStop();
		m_bButtonUsed = false;
		m_bUserButtonStop = false;
	}
}

// Setters
void ShutterClass::EnableMotor(const bool newState)
{
	if (!newState) {
		digitalWrite(STEPPER_ENABLE_PIN, M_DISABLE);
	}
	else {
		digitalWrite(STEPPER_ENABLE_PIN, M_ENABLE);
	}
}

// Movers
void ShutterClass::Open()
{
	m_nVolts = MeasureVoltage();
	if(GetVoltsAreLow()) // do not try to open if we're already at low voltage
		return;

	if (digitalRead(OPENED_PIN) == 0) {
		DBPrintln("[Open()] shutterState = OPEN");
		shutterState = OPEN;
		return;
	}

	shutterState = OPENING;
	DBPrintln("[Open()] shutterState = OPENING");
	MoveRelative(160000000L);
}

void ShutterClass::Close()
{
	if (digitalRead(CLOSED_PIN) == 0) {
		DBPrintln("[Close()] shutterState = CLOSE");
		shutterState = CLOSED;
		return;
	}
	shutterState = CLOSING;
	DBPrintln("[Close()]  shutterState = CLOSING");
	MoveRelative(-160000000L);
}


void ShutterClass::Abort()
{
	m_bButtonUsed = true; // will stop and not try to finish close/open
	stepper.stop();
}

void ShutterClass::Run()
{
	int sw1,sw2;

	if (m_batteryCheckTimer.elapsed() >= m_nBatteryCheckInterval) {
		DBPrintln("Measuring Battery");
		m_nVolts = MeasureVoltage();
		DBPrintln("Voltage : " + String(m_nVolts));

		if(GetVoltsAreLow() && shutterState!=CLOSED) {
			DBPrintln("Voltage is low, closing");
			Close();
		}
		m_batteryCheckTimer.reset();
	}

	stepper.run(); // RP2040 core1

	if (stepper.isRunning()) {
		m_bWasRunning = true;
		return;
	}

	if (m_bWasRunning) { // This only runs once after stopping.
		if (digitalRead(CLOSED_PIN) == 0) {
			shutterState = CLOSED;
			stepper.setCurrentPosition(0);
			DBPrintln("Stopped at closed position");
		}
		else if (digitalRead(OPENED_PIN) == 0) {
			shutterState = OPEN;
			DBPrintln("Stopped at open position");
		}
		else if((shutterState == FINISHING_CLOSE || shutterState==CLOSING) && !m_bUserButtonStop) {
			//motor stopped for some reason
			DBPrintln("motor stopped for some reason but we're not closed... closing");
			Close();
			return;
		}
		else if((shutterState == FINISHING_OPEN || shutterState==OPENING) && !m_bUserButtonStop) {
			//motor stopped for some reason
			DBPrintln("motor stopped for some reason but we're not open... opening");
			Open();
			return;
		}
		EnableMotor(false);
		m_bWasRunning = false;
	}
	else { // make sure the state are accurate.
		sw1 = digitalRead(CLOSED_PIN);
		sw2 = digitalRead(OPENED_PIN);

		if (sw1 == LOW && sw2 == HIGH) {
			shutterState = CLOSED;
			}
		else if (sw1 == HIGH && sw2 == LOW) {
			shutterState = OPEN;
			}
	}
}


void ShutterClass::motorStop()
{
	stepper.stop();

}


void ShutterClass::motorMoveTo(const long newPosition)
{
	EnableMotor(true);
	stepper.moveTo(newPosition);
}

void ShutterClass::motorMoveRelative(const long amount)
{
	EnableMotor(true);
	stepper.move(amount);
}
