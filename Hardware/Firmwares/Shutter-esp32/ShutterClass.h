//
// RTI-Zone Dome shutter firmware.
//
//  Copyright © 2024 Rodolphe Pineau. All rights reserved.
//
//

#include <Preferences.h>
#include <nvs_flash.h>
#include <FastAccelStepper.h>
#include "config.h"
#include "StopWatch.h"



FastAccelStepperEngine engine = FastAccelStepperEngine();
FastAccelStepper *stepper = NULL;

// All possible Shutter state, including option for a dropout
enum ShutterStates { OPEN, CLOSED, OPENING, CLOSING, TOP_OPEN, TOP_CLOSED, TOP_OPENING, TOP_CLOSING, BOTTOM_OPEN, BOTTOM_CLOSED, BOTTOM_OPENING, BOTTOM_CLOSING, ERROR, FINISHING_OPEN, FINISHING_CLOSE };
enum LowerShutterDir { ACTUATOR_CLOSE, ACTUATOR_OPEN };
enum LowerShutterAction { ACTUATOR_OFF, ACTUATOR_ON };
enum shutterOrder {BOTTOM_FIRST = 0, TOP_FIRST};

volatile ShutterStates shutterState;
volatile ShutterStates topShutterState;
volatile ShutterStates bottomShutterState;
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
	void		SetStepsPerStroke(const unsigned long, bool bSave=true);

	bool        GetVoltsAreLow();
	String      GetVoltString();
	void        SetVoltsFromString(const String);

	unsigned long   getWatchdogInterval();
	void            SetWatchdogInterval(const unsigned long);

	// Move
	void		DoButtons();
	void		Open();
	void		Close();
	void		Run();
	void		motorStop();
	void 		Stop();
	void		motorMoveTo(const long newPosition);
	void		motorMoveRelative(const long amount);
	// double shutter methods
	void		setDoubleShutterEnable(bool bEnable);
	bool		getDoubleShutterEnable();
	void		setOpenOrder(bool bBottomfirst);
	int			getOpenOrder();

	// persistent data
	void		restoreDefaultMotorSettings();
	void		resetAlltoDefault();

	// interrupts
	void		ClosedInterrupt();
	void		OpenInterrupt();
	void		LowerClosedInterrupt();
	void		LowerOpenInterrupt();
	volatile bool     m_bButtonUsed;

	void    	Abort();

	String      getSSID();
	void        setSSID(String sSSID);
	void		getWiFiConfig(WIFIConfig &config);

	int             MeasureVoltage();
	int             GetVolts();            // cached reading, no ADC access

private:

	void            UpdateVoltageState();  // call after every MeasureVoltage()
	int             m_nLowVoltCount = 0;   // consecutive low readings
	bool            m_bVoltsAreLow = false;// debounced state

	Configuration   m_Config;
	Preferences 	m_preferences;

	float           m_fAdcConvert;
	int             m_nVolts;
	StopWatch       m_batteryCheckTimer;
	unsigned long   m_nBatteryCheckInterval;
	bool            m_bAborted;

	void			clearPendingActions();
	volatile bool m_bPendingOpenBottom  = false;
	volatile bool m_bPendingCloseBottom = false;
	volatile bool m_bPendingOpenTop     = false;
	volatile bool m_bPendingCloseTop    = false;

	void			openTop();
	void			closeTop();
	void			openBottom();
	void			closeBottom();
	void 			LoadConfig();
};


ShutterClass::ShutterClass()
{
	int sw1, sw2;

	shutterState = ERROR;

	m_fAdcConvert = RES_MULT * (AD_REF / 4095.0f) * 100.0f;

	DBPrintln("configuring pins");

	// Input pins
	pinMode(CLOSED_PIN,						INPUT);
	pinMode(OPEN_PIN,							INPUT);
	pinMode(BUTTON_OPEN,					INPUT);
	pinMode(BUTTON_CLOSE,					INPUT);
	pinMode(VOLTAGE_MONITOR_PIN,	INPUT);
	// dual shutter mode
	pinMode(LOWER_CLOSED_PIN,			INPUT);
	pinMode(LOWER_OPENED_PIN,			INPUT); 

	// Ouput pins
	pinMode(STEP_PIN,							OUTPUT);
	pinMode(DIRECTION_PIN,				OUTPUT);
	pinMode(STEPPER_ENABLE_PIN,		OUTPUT);

	pinMode(LOWER_DIR,						OUTPUT);
	pinMode(LOWER_ENABLE,					OUTPUT);

	digitalWrite(LOWER_ENABLE, ACTUATOR_OFF);   // don't drive on boot
	
	DBPrintln("Loading config");

	LoadConfig();

	DBPrintln("Configuring stepper");

	engine.init();
   	stepper = engine.stepperConnectToPin(STEP_PIN);
	stepper->setDirectionPin(DIRECTION_PIN,(!m_Config.reversed));
	stepper->setEnablePin(STEPPER_ENABLE_PIN);
	stepper->setAutoEnable(true);
	stepper->setSpeedInHz(m_Config.maxSpeed);  //  steps/s
	stepper->setAcceleration(m_Config.acceleration);    //  steps/s²
	SetStepsPerStroke(m_Config.stepsPerStroke, false);

	// reset all timers
	m_nBatteryCheckInterval = BATTERY_CHECK_INTERVAL;
	m_batteryCheckTimer.reset();

	// read initial shutter state
	sw1 = digitalRead(CLOSED_PIN);
	sw2 = digitalRead(OPEN_PIN);

	shutterState = ERROR;
	if (sw1 == LOW && sw2 == HIGH)
		shutterState = CLOSED;
	else if (sw1 == HIGH && sw2 == LOW)
		shutterState = OPEN;

	if(m_Config.bHasDropShutter) {
		topShutterState    = (digitalRead(CLOSED_PIN) == LOW) ? TOP_CLOSED : (digitalRead(OPEN_PIN) == LOW) ? TOP_OPEN    : ERROR;
		bottomShutterState = (digitalRead(LOWER_CLOSED_PIN) == LOW) ? BOTTOM_CLOSED : (digitalRead(LOWER_OPENED_PIN) == LOW) ? BOTTOM_OPEN : ERROR;
	}
	m_bAborted=false;
	m_bButtonUsed = false;
	m_nVolts = MeasureVoltage();
	UpdateVoltageState();
}

void IRAM_ATTR ShutterClass::OpenInterrupt()
{
    if(m_Config.bHasDropShutter) {
        if(topShutterState == TOP_OPENING) {
            motorStop();
            if(!m_Config.bBottomShutterOpenFirst)
                m_bPendingOpenBottom = true;
            else
                topShutterState = FINISHING_OPEN;
        }
    }
    else if(shutterState == OPENING) {
        motorStop();
        shutterState = FINISHING_OPEN;
    }
}

void IRAM_ATTR ShutterClass::ClosedInterrupt()
{
	if(m_Config.bHasDropShutter) {
		if(topShutterState == TOP_CLOSING) {
			motorStop();
			if(m_Config.bBottomShutterOpenFirst) {  // bottom opens first -> bottom closes last
				m_bPendingCloseBottom = true;
			}
			else {                                  // top closes last -> we're done
				topShutterState = FINISHING_CLOSE;
			}
		}
	}
	else {
		if(shutterState == CLOSING) {
			motorStop();
			shutterState = FINISHING_CLOSE;
		}
	}
}


void IRAM_ATTR ShutterClass::LowerClosedInterrupt()
{
	if(bottomShutterState == BOTTOM_CLOSING) {
		digitalWrite(LOWER_ENABLE, ACTUATOR_OFF);
		if(!m_Config.bBottomShutterOpenFirst) {  // top opens first -> top closes last
			m_bPendingCloseTop = true;
		}
		else {                                   // bottom closes last -> we're done
			bottomShutterState = BOTTOM_CLOSED;
		}
	}
}


void IRAM_ATTR ShutterClass::LowerOpenInterrupt()
{
	if(bottomShutterState == BOTTOM_OPENING) {
		digitalWrite(LOWER_ENABLE, ACTUATOR_OFF);
		if(m_Config.bBottomShutterOpenFirst) {   // bottom opened first -> top is next
			m_bPendingOpenTop = true;
		}
		else {                                   // bottom opened last -> we're done
			bottomShutterState = BOTTOM_OPEN;
		}
	}
}

void ShutterClass::clearPendingActions()
{
	m_bPendingOpenBottom  = false;
	m_bPendingCloseBottom = false;
	m_bPendingOpenTop     = false;
	m_bPendingCloseTop    = false;	
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
	m_Config.cutoffVolts = m_preferences.getInt("cutoffVolts",DEFAULT_CUT_OFF_VOLTS);
	m_Config.watchdogInterval = m_preferences.getULong("wdInterval",DEFAULT_WATCHDOG_INTERVAL);
	m_Config.bHasDropShutter = m_preferences.getBool("hasDropShutter", false);
	m_Config.bBottomShutterOpenFirst = m_preferences.getBool("botShutFirst", true); // this generaly the case.
	m_Config.wifiIpConfig.ip.fromString(m_preferences.getString("wifi_ip","172.31.255.2"));
	m_Config.wifiIpConfig.sSSID = m_preferences.getString("wifiSSID", "RTIShutter");
	if(m_Config.wifiIpConfig.sSSID.length()<8) {
		m_Config.wifiIpConfig.sSSID = "RTIShutter";	// set to default as something bad was set
		m_preferences.putString("wifiSSID", m_Config.wifiIpConfig.sSSID );
	}

	m_Config.wifiIpConfig.sPassword = m_preferences.getString("wifiPassword", "RTIShutter");

	DBPrintln("m_Config.stepsPerStroke       : " + String(m_Config.stepsPerStroke));
	DBPrintln("m_Config.acceleration         : " + String(m_Config.acceleration));
	DBPrintln("m_Config.maxSpeed             : " + String(m_Config.maxSpeed));
	DBPrintln("m_Config.reversed             : " + String(m_Config.reversed?"Yes":"No"));
	DBPrintln("m_Config.cutoffVolts          : " + String(m_Config.cutoffVolts));
	DBPrintln("m_Config.watchdogInterval     : " + String(m_Config.watchdogInterval));
	DBPrintln("m_Config.bHasDropShutter      : " + String(m_Config.bHasDropShutter?"Yes":"No"));
	DBPrintln("m_Config.bBottomShutterOpenFirst : " + String(m_Config.bBottomShutterOpenFirst?"Yes":"No"));
	DBPrintln("wifiIpConfig.ip               : " + IpAddress2String(m_Config.wifiIpConfig.ip));
	DBPrintln("wifiIpConfig.sSSID            : " + String(m_Config.wifiIpConfig.sSSID));
	DBPrintln("wifiIpConfig.sPassword        : " + String(m_Config.wifiIpConfig.sPassword));

	if(m_Config.watchdogInterval > MAX_WATCHDOG_INTERVAL) {
		m_Config.watchdogInterval = MAX_WATCHDOG_INTERVAL;
		m_preferences.putULong("wdInterval", m_Config.watchdogInterval);
	}
	if(m_Config.watchdogInterval < MIN_WATCHDOG_INTERVAL) {
		m_Config.watchdogInterval = MIN_WATCHDOG_INTERVAL;
		m_preferences.putULong("wdInterval", m_Config.watchdogInterval);
	}
	m_preferences.end();
}


String ShutterClass::getSSID()
{
	return m_Config.wifiIpConfig.sSSID;
}

void ShutterClass::setSSID(String sSSID)
{
	if(sSSID.length()<8) {
		sSSID = "RTIShutter";	// set to default
	}
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

void ShutterClass::resetAlltoDefault()
{
	DBPrintln("Resetting do factory defaults");
	DBPrintln("Initializing NVS");
	nvs_flash_erase();
	nvs_flash_init();
	m_preferences.begin("RTI_Shutter", false);
	m_preferences.putBool("nvsInit", true);
	m_preferences.end();
	ESP.restart();

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
	stepper->setAcceleration(m_Config.acceleration);    //  steps/s²
	m_preferences.begin("RTI_Shutter", false);
	m_preferences.putInt("acceleration", accel);
	m_preferences.end();
}

int ShutterClass::GetMaxSpeed()
{
	return m_Config.maxSpeed;
}

void ShutterClass::SetMaxSpeed(const int speed)
{
	m_Config.maxSpeed = speed;
	stepper->setSpeedInHz(m_Config.maxSpeed);  //  steps/s
	m_preferences.begin("RTI_Shutter", false);
	m_preferences.putInt("maxSpeed", speed);
	m_preferences.end();
}

long ShutterClass::GetPosition()
{
	return stepper->getCurrentPosition();
}

void ShutterClass::GotoPosition(const unsigned long newPos)
{
	uint64_t currentPos = stepper->getCurrentPosition();
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
	return PositionToAltitude(stepper->getCurrentPosition());
}

bool ShutterClass::GetReversed()
{
	return m_Config.reversed;
}

void ShutterClass::SetReversed(const bool reversed)
{
	m_Config.reversed = reversed;
	stepper->setDirectionPin(DIRECTION_PIN,(!reversed));
	m_preferences.begin("RTI_Shutter", false);
	m_preferences.putBool("reversed", reversed);
	m_preferences.end();
}

int ShutterClass::GetEndSwitchStatus()
{
	int result= ERROR;

	if (digitalRead(CLOSED_PIN) == LOW)
		result = CLOSED;

	if (digitalRead(OPEN_PIN) == LOW)
		result = OPEN;
	return result;
}

int ShutterClass::GetState()
{
	return int(shutterState);
}

unsigned long ShutterClass::GetStepsPerStroke()
{
	return m_Config.stepsPerStroke;
}

void ShutterClass::SetStepsPerStroke(const unsigned long newSteps, bool bSave)
{
	m_Config.stepsPerStroke = newSteps;
	if(bSave) {
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


// Pure readers: these must NOT touch the ADC.
// They are called from the WiFi task on every ping, and sampling GPIO36 while
// the radio is active produces spurious low readings. m_nVolts is refreshed by
// Run()'s battery check and by Open(), each followed by UpdateVoltageState().
inline bool ShutterClass::GetVoltsAreLow()
{
	return m_bVoltsAreLow;
}

int ShutterClass::GetVolts()
{
	return m_nVolts;
}

String ShutterClass::GetVoltString()
{
	return String(m_nVolts) + "," + String(m_Config.cutoffVolts);
}

// Debounce: three consecutive low readings before we believe it. A single
// anomalous sample must never close the shutter or tell the rotator to park.
void ShutterClass::UpdateVoltageState()
{
	if(m_nVolts <= m_Config.cutoffVolts) {
		if(m_nLowVoltCount < 3)
			m_nLowVoltCount++;
	}
	else {
		m_nLowVoltCount = 0;
	}
	m_bVoltsAreLow = (m_nLowVoltCount >= 3);
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
	const int nSamples = 15;
	uint16_t s[nSamples];

	// GPIO36 (SENSOR_VP) is subject to an ESP32 erratum: powering certain RTC
	// peripherals - including SAR ADC2, which the WiFi stack uses for RF
	// calibration - pulls this input low for ~80ns. A mean folds those glitches
	// into the result, so take the median instead: up to 7 of the 15 samples
	// can be corrupted and the answer is still correct.
	for(int i = 0; i < nSamples; i++) {
		s[i] = analogRead(VOLTAGE_MONITOR_PIN);
		delayMicroseconds(50);        // let the sampling cap settle
	}

	for(int i = 1; i < nSamples; i++) {   // insertion sort
		uint16_t k = s[i];
		int j = i - 1;
		while(j >= 0 && s[j] > k) {
			s[j+1] = s[j];
			j--;
		}
		s[j+1] = k;
	}

	return int(s[nSamples/2] * m_fAdcConvert + 0.5f);
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
	m_preferences.putULong("wdInterval", m_Config.watchdogInterval);
	m_preferences.end();
}

// INPUTS
void IRAM_ATTR ShutterClass::DoButtons()
{
	int sw1, sw2, sw3, sw4;

	sw1 = digitalRead(BUTTON_OPEN);
	sw2 = digitalRead(BUTTON_CLOSE);

	sw3 = digitalRead(CLOSED_PIN);
	sw4 = digitalRead(OPEN_PIN);

	// shutter is between open and close and we want to open
	if(sw1 == LOW  && sw3 == HIGH && sw4 == HIGH ) {
		motorStop();
		MoveRelative(MOVE_DISTANCE_IN_STEPS );
		shutterState = OPENING;
		m_bButtonUsed = true;
		m_bAborted = false;
		buttonStopTimer.reset();
	}
	// shutter is between open and close and we want to close
	else if(sw2 == LOW && sw3 == HIGH && sw4 == HIGH ) {
		motorStop();
		shutterState = CLOSING;
		MoveRelative(-MOVE_DISTANCE_IN_STEPS );
		m_bButtonUsed = true;
		m_bAborted = false;
		buttonStopTimer.reset();
	}
	// button open pressed and we're closed
	else if (sw1 == LOW && sw3 == LOW && sw4 == HIGH) {
		MoveRelative(MOVE_DISTANCE_IN_STEPS );
		shutterState = OPENING;
		m_bButtonUsed = true;
		m_bAborted = false;
		buttonStopTimer.reset();
	}
	// button close pressed and we're open
	else if (sw2 == LOW && sw3 == HIGH && sw4 == LOW) {
		MoveRelative(-MOVE_DISTANCE_IN_STEPS );
		shutterState = CLOSING;
		m_bButtonUsed = true;
		m_bAborted = false;
		buttonStopTimer.reset();
	}
	else {
		buttonStopTimer.reset();
		motorStop();
		m_bButtonUsed = false;
		m_bAborted = false;
	}
}

// Movers
void ShutterClass::openTop()
{
	DBPrintln("[openTop()] Top shutterState = OPENING");
	if (digitalRead(OPEN_PIN) == 0) {
		DBPrintln("[openTop()] shutterState = OPEN");
		if(m_Config.bHasDropShutter) {
			topShutterState = TOP_OPEN;
		}
		else {
			shutterState = OPEN;
		}
		return;
	}

	if(m_Config.bHasDropShutter) {
		topShutterState = TOP_OPENING;
	}
	else {
		shutterState = OPENING;
	}
	MoveRelative(MOVE_DISTANCE_IN_STEPS );
}

void ShutterClass::closeTop()
{
	DBPrintln("[closeTop()] Top shutterState = CLOSING");
	if (digitalRead(CLOSED_PIN) == 0) {
		DBPrintln("[closeTop()] shutterState = OPEN");
		if(m_Config.bHasDropShutter) {
			topShutterState = TOP_CLOSED;
		}
		else {
			shutterState = CLOSED;
		}
		return;
	}

	if(m_Config.bHasDropShutter) {
		topShutterState = TOP_CLOSING;
	}
	else {
		shutterState = CLOSING;
	}
	MoveRelative(-MOVE_DISTANCE_IN_STEPS );
}

void ShutterClass::openBottom()
{
	bottomShutterState = BOTTOM_OPENING;
	digitalWrite(LOWER_DIR,ACTUATOR_OPEN);
	digitalWrite(LOWER_ENABLE,ACTUATOR_ON);
}

void ShutterClass::closeBottom()
{
	bottomShutterState = BOTTOM_CLOSING;
	digitalWrite(LOWER_DIR,ACTUATOR_CLOSE);
	digitalWrite(LOWER_ENABLE,ACTUATOR_ON);
}

void ShutterClass::Open()
{
	m_bAborted = false;
	clearPendingActions();
	m_nVolts = MeasureVoltage();   // fresh reading before committing to open
	UpdateVoltageState();
	// Make sure we never open if a low voltage is detected
	// This bypass the other low voltage debouncing
	// Worst case scenario we prevent an open and the user/script can retry.
	if(m_nVolts <= m_Config.cutoffVolts)
        return;

	if(m_Config.bHasDropShutter) {
		shutterState = OPENING;
		if(m_Config.bBottomShutterOpenFirst ) {
			openBottom();
		}
		else {
			openTop();
		}
	} else {
		// single shutter mode
		openTop();
	}
}


void ShutterClass::Close()
{
	m_bAborted = false;
	clearPendingActions();

	if(m_Config.bHasDropShutter ) {
			shutterState = CLOSING;
			if(m_Config.bBottomShutterOpenFirst) { // open bottom first = close bottom last
				closeTop();
			}
			else {
				closeBottom();
			}
	} else {
		// single shutter mode
		closeTop();
	}
}


void ShutterClass::Abort()
{
	clearPendingActions();
	m_bButtonUsed = true;
	m_bAborted = true; //don't try to continue open/close
	Stop();
	digitalWrite(LOWER_ENABLE, ACTUATOR_OFF);

	if(m_Config.bHasDropShutter) {
		topShutterState    = (digitalRead(CLOSED_PIN)       == LOW) ? TOP_CLOSED
		                   : (digitalRead(OPEN_PIN)         == LOW) ? TOP_OPEN    : ERROR;
		bottomShutterState = (digitalRead(LOWER_CLOSED_PIN) == LOW) ? BOTTOM_CLOSED
		                   : (digitalRead(LOWER_OPENED_PIN) == LOW) ? BOTTOM_OPEN : ERROR;
		
		shutterState = ERROR;
	}
	else {
		shutterState = (digitalRead(CLOSED_PIN) == LOW) ? CLOSED
	             : (digitalRead(OPEN_PIN)   == LOW) ? OPEN : ERROR;
	}
}


void ShutterClass::Run()
{
	int sw1,sw2;

	// deferred actions from ISRs
	if(m_bPendingOpenBottom)  { m_bPendingOpenBottom  = false; openBottom();  }
	if(m_bPendingCloseBottom) { m_bPendingCloseBottom = false; closeBottom(); }
	if(m_bPendingOpenTop)     { m_bPendingOpenTop     = false; openTop();     }
	if(m_bPendingCloseTop)    { m_bPendingCloseTop    = false; closeTop();    }


	if (m_batteryCheckTimer.elapsed() >= m_nBatteryCheckInterval) {
		m_nVolts = MeasureVoltage();
		UpdateVoltageState();

		if(GetVoltsAreLow() && shutterState!=CLOSED) {
			Close();
		}
		m_batteryCheckTimer.reset();
	}



	// single shutter
	if(!m_Config.bHasDropShutter ) {
		if (stepper->isRunning()) {
			m_bWasRunning = true;
			return;
		}
		if (m_bWasRunning) { // This only runs once after stopping.
			DBPrintln("m_bWasRunning 1 SHutterState : " + String(shutterState));

			if (digitalRead(CLOSED_PIN) == 0) {
				stepper->setCurrentPosition(0);
				shutterState = CLOSED;
				DBPrintln("Stopped at closed position");
				DBPrintln("m_bWasRunning 2 SHutterState : " + String(shutterState));
			}
			else if (digitalRead(OPEN_PIN) == 0) {
				shutterState = OPEN;
				DBPrintln("Stopped at open position");
				DBPrintln("m_bWasRunning 3 SHutterState : " + String(shutterState));
			}
			else if((shutterState == FINISHING_CLOSE || shutterState==CLOSING) && !m_bAborted) {
				//motor stopped for some reason
				DBPrintln("motor stopped for some reason but we're not closed... closing");
				Close();
				DBPrintln("m_bWasRunning 4 SHutterState : " + String(shutterState));
				return;
			}
			else if((shutterState == FINISHING_OPEN || shutterState==OPENING) && !m_bAborted) {
				//motor stopped for some reason
				DBPrintln("motor stopped for some reason but we're not open... opening");
				Open();
				DBPrintln("m_bWasRunning 5 SHutterState : " + String(shutterState));
				return;
			}
			m_bWasRunning = false;
			DBPrintln("m_bWasRunning final SHutterState : " + String(shutterState));
		}
	}

	else { // dual shutter.
		if(shutterState==CLOSING) {
			if(topShutterState == TOP_CLOSED && bottomShutterState == BOTTOM_CLOSED) {
				shutterState = CLOSED;
				return; // closed, we're done
			}

			// check if top shutter is moving
			if (stepper->isRunning()) {
				m_bWasRunning = true;
				return; // still closing the top
			}

			if(m_bWasRunning) {
				if (digitalRead(CLOSED_PIN) == 0) {
					topShutterState = TOP_CLOSED;
					stepper->setCurrentPosition(0);
				}
				else if((topShutterState == FINISHING_CLOSE || topShutterState==TOP_CLOSING) && !m_bAborted) {
					//motor stopped for some reason
					DBPrintln("Top motor stopped for some reason but we're not closed... closing");
					closeTop();
					DBPrintln("m_bWasRunning topShutterState : " + String(topShutterState));
					return;
				}
			}
			m_bWasRunning = false;

		}
		else if(shutterState==OPENING) {
			if(topShutterState == TOP_OPEN && bottomShutterState == BOTTOM_OPEN) {
				shutterState = OPEN;
				return; // open, we're done
			}

			// check if top shutter is moving
			if (stepper->isRunning()) {
				m_bWasRunning = true;
				return; // still opening the top
			}

			if(m_bWasRunning) {
				if (digitalRead(OPEN_PIN) == 0) {
					topShutterState = TOP_OPEN;
				}
				else if((topShutterState == FINISHING_OPEN || topShutterState==TOP_OPENING) && !m_bAborted) {
					//motor stopped for some reason
					DBPrintln("Top motor stopped for some reason but we're not closed... closing");
					openTop();
					DBPrintln("m_bWasRunning topShutterState : " + String(topShutterState));
					return;
				}
			}
			m_bWasRunning = false;
		}
	}
}


void ShutterClass::motorStop()
{
	stepper->stopMove();

}

void ShutterClass::Stop()
{
	stepper->forceStop();

}

void ShutterClass::motorMoveTo(const long newPosition)
{
	stepper->moveTo(newPosition);
}

void ShutterClass::motorMoveRelative(const long amount)
{
	stepper->move(amount);
}

// double shutter methods
void ShutterClass::setDoubleShutterEnable(bool bEnable)
{
	m_Config.bHasDropShutter = bEnable;
	m_preferences.begin("RTI_Shutter", false);
	m_preferences.putBool("hasDropShutter", m_Config.bHasDropShutter);
	m_preferences.end();
	
}

bool ShutterClass::getDoubleShutterEnable()
{
	return m_Config.bHasDropShutter;
}

void ShutterClass::setOpenOrder(bool bBottomfirst)
{
	m_Config.bBottomShutterOpenFirst = bBottomfirst;
	m_preferences.begin("RTI_Shutter", false);
	m_preferences.putBool("botShutFirst", m_Config.bBottomShutterOpenFirst);
	m_preferences.end();

}

int ShutterClass::getOpenOrder()
{
	return m_Config.bBottomShutterOpenFirst?1:0;
}
