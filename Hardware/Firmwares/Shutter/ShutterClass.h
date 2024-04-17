//
// RTI-Zone Dome Rotator firmware. Based on https://github.com/nexdome/Automation/tree/master/Firmwares
// As I contributed to the "old" 2,x firmware and was somewhat familiar with it I decided to reuse it and
// fix most of the known issues. I also added some feature related to XBee init and reset.
// This also is meant to run on an Arduino DUE as we put the AccelStepper run() call in an interrupt
//

#ifdef USE_EXT_EEPROM
#include <Wire.h>
#define EEPROM_ADDR 0x50
#define I2C_CHUNK_SIZE  8
#else
#include <DueFlashStorage.h>
DueFlashStorage dueFlashStorage;
#endif

#include <AccelStepper.h>
#include "StopWatch.h"

// Debug printing, uncomment #define DEBUG to enable
// #define DEBUG
#ifdef DEBUG
#define DBPrint(x)   if(DebugPort) DebugPort.print(x)
#define DBPrintln(x) if(DebugPort) DebugPort.println(x)
#else
#define DBPrint(x)
#define DBPrintln(x)
#endif // DEBUG

#define DEFAULT_PANID   0x4242

// Pin configuration
#ifndef TEENY_3_5
// Arduino boards
#define     CLOSED_PIN               2
#define     OPENED_PIN               3
#define     BUTTON_OPEN              5
#define     BUTTON_CLOSE             6
#define     STEPPER_ENABLE_PIN      10
#define     STEPPER_DIRECTION_PIN   11
#define     STEPPER_STEP_PIN        12
#define     DROPOUT_OPEN_PIN        13
#define     BOTTOM_CLOSE_PIN         7
#define     BOTTOM_PWM_PIN           9
#define     BOTTOM_DIR_PIN          A4
#define     BUFFERN_EN              57  // Digital output to enable 74LVC245 buffer on prototypes
#else
// Teensy boards > 3.5
#define     CLOSED_PIN               4
#define     OPENED_PIN               5
#define     BUTTON_OPEN              7
#define     BUTTON_CLOSE             8
#define     STEPPER_ENABLE_PIN       9
#define     STEPPER_DIRECTION_PIN   10
#define     STEPPER_STEP_PIN         6
#endif

#define     EEPROM_LOCATION         0  // not used with Arduino Due flash
#define     EEPROM_SIGNATURE        2645

#define MIN_WATCHDOG_INTERVAL       60000
#define MAX_WATCHDOG_INTERVAL       300000

#define BATTERY_CHECK_INTERVAL      60000   // check battery once a minute

#define VOLTAGE_MONITOR_PIN A0
#define AD_REF      3.3
#define RES_MULT    5.0 // resistor voltage divider on the shield

#define M_ENABLE    HIGH
#define M_DISABLE   LOW

// DM556T stepper controller min pulse width  = 2.5uS
// #define MIN_PULSE_WIDTH 3

// ISD02/04/08 stepper controller min pulse width = 5uS at 1600rev/s (8 microsteps).
// TB6600 tepper controller min pulse width = 5uS
#define MIN_PULSE_WIDTH 5

typedef struct ShutterConfiguration {
	int             signature;
	unsigned long   stepsPerStroke;
	int             acceleration;
	int             maxSpeed;
	bool            reversed;
	int             cutoffVolts;
	unsigned long   watchdogInterval;
	int             panid;
	bool            bHasDropShutter;
	bool            bTopShutterOpenFirst;
} Configuration;


AccelStepper stepper(AccelStepper::DRIVER, STEPPER_STEP_PIN, STEPPER_DIRECTION_PIN);

// All possible Shutter state, including option got a dropout
enum ShutterStates { OPEN, CLOSED, OPENING, CLOSING, BOTTOM_OPEN, BOTTOM_CLOSED, BOTTOM_OPENING, BOTTOM_CLOSING, ERROR, FINISHING_OPEN, FINISHING_CLOSE };
volatile ShutterStates shutterState = ERROR;

StopWatch watchdogTimer;
StopWatch buttonStopTimer;

/*
 * As demonstrated by RCArduino and modified by BKM:
 * pick clock that provides the least error for specified frequency.
 * https://github.com/SomeRandomGuy/DueTimer
 * https://github.com/ivanseidel/DueTimer
 */
uint8_t pickClock(uint32_t frequency, uint32_t& retRC)
{
	/*
		Timer       Definition
		TIMER_CLOCK1    MCK/2
		TIMER_CLOCK2    MCK/8
		TIMER_CLOCK3    MCK/32
		TIMER_CLOCK4    MCK/128
	*/
	struct {
		uint8_t flag;
		uint8_t divisor;
	} clockConfig[] = {
		{ TC_CMR_TCCLKS_TIMER_CLOCK1, 2 },
		{ TC_CMR_TCCLKS_TIMER_CLOCK2, 8 },
		{ TC_CMR_TCCLKS_TIMER_CLOCK3, 32 },
		{ TC_CMR_TCCLKS_TIMER_CLOCK4, 128 }
	};
	float ticks;
	float error;
	int clkId = 3;
	int bestClock = 3;
	float bestError = 1.0;
	do
	{
		ticks = (float) VARIANT_MCK / (float) frequency / (float) clockConfig[clkId].divisor;
		error = abs(ticks - round(ticks));
		if (abs(error) < bestError)
		{
			bestClock = clkId;
			bestError = error;
		}
	} while (clkId-- > 0);
	ticks = (float) VARIANT_MCK / (float) frequency / (float) clockConfig[bestClock].divisor;
	retRC = (uint32_t) round(ticks);
	return clockConfig[bestClock].flag;
}


void startTimer(Tc *tc, uint32_t channel, IRQn_Type irq, uint32_t frequency)
{
	uint32_t rc = 0;
	uint8_t clock;
	pmc_set_writeprotect(false);
	pmc_enable_periph_clk((uint32_t)irq);
	clock = pickClock(frequency, rc);

	TC_Configure(tc, channel, TC_CMR_WAVE | TC_CMR_WAVSEL_UP_RC | clock);
	TC_SetRA(tc, channel, rc/2); //50% high, 50% low
	TC_SetRC(tc, channel, rc);
	TC_Start(tc, channel);
	tc->TC_CHANNEL[channel].TC_IER=TC_IER_CPCS;
	tc->TC_CHANNEL[channel].TC_IDR=~TC_IER_CPCS;

	NVIC_EnableIRQ(irq);
}

void stopTimer(Tc *tc, uint32_t channel, IRQn_Type irq)
{
	NVIC_DisableIRQ(irq);
	TC_Stop(tc, channel);
}

// DUE stepper callback
void TC3_Handler()
{
	TC_GetStatus(TC1, 0);
	stepper.run();
}

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

	unsigned long   GetStepsPerStroke();
	void            SetStepsPerStroke(const unsigned long);

	bool        GetVoltsAreLow();
	String      GetVoltString();
	void        SetVoltsFromString(const String);

	String      GetPANID();
	void        setPANID(const String panID);

	unsigned long   getWatchdogInterval();
	void            SetWatchdogInterval(const unsigned long);

	// Move
	void        DoButtons();
	void        EnableMotor(const bool);
	void        Open();
	void        Close();
	void        Run();
	static void motorStop();
	void        motorMoveTo(const long newPosition);
	void        motorMoveRelative(const long amount);
	void        stopInterrupt();

	// persistent data
	void        LoadFromEEProm();
	void        SaveToEEProm();
	int         restoreDefaultMotorSettings();

	// interrupts
	void     ClosedInterrupt();
	void     OpenInterrupt();
	volatile bool     m_bButtonUsed;

	void    bufferEnable(bool bEnable);
	void    Abort();

private:

	Configuration   m_Config;
	float           m_fAdcConvert;
	int             m_nVolts;
	StopWatch       m_batteryCheckTimer;
	unsigned long   m_nBatteryCheckInterval;
	bool            m_bUserButtonStop;

	int             MeasureVoltage();
	void            SetDefaultConfig();

	bool        m_bDoEEPromSave;
#ifdef USE_EXT_EEPROM
	// eeprom
	byte        m_EEPROMpageSize;

	byte        readEEPROMByte(int deviceaddress, unsigned int eeaddress);
	void        readEEPROMBuffer(int deviceaddress, unsigned int eeaddress, byte *buffer, int length);
	void        readEEPROMBlock(int deviceaddress, unsigned int address, byte *data, int offset, int length);
	void        writeEEPROM(int deviceaddress, unsigned int address, byte *data, int length);
	void        writeEEPROMBlock(int deviceaddress, unsigned int address, byte *data, int offset, int length);
#endif

};


ShutterClass::ShutterClass()
{
	int sw1, sw2;

#ifdef USE_EXT_EEPROM
	DBPrintln("Using external AT24AA128 eeprom");
	Wire1.begin();
	// AT24AA128 page size is 64 byte
	m_EEPROMpageSize = 64;
#endif

	m_fAdcConvert = RES_MULT * (AD_REF / 1023.0) * 100;

	// Input pins
	pinMode(CLOSED_PIN,             INPUT);
	pinMode(OPENED_PIN,             INPUT);
	pinMode(BUTTON_OPEN,            INPUT);
	pinMode(BUTTON_CLOSE,           INPUT);
	pinMode(VOLTAGE_MONITOR_PIN,    INPUT);

	// Ouput pins
	pinMode(STEPPER_STEP_PIN,       OUTPUT);
	pinMode(STEPPER_DIRECTION_PIN,  OUTPUT);
	pinMode(STEPPER_ENABLE_PIN,     OUTPUT);

	bufferEnable(true);
	// old board buffer enable
	// pinMode(A3,       OUTPUT);
	// digitalWrite(A3, LOW);

	LoadFromEEProm();

	m_bDoEEPromSave = false;  // we just read the config, no need to resave all the value we're setting
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
	m_bDoEEPromSave = true;
}

void ShutterClass::ClosedInterrupt()
{
	// debounce
	if (digitalRead(CLOSED_PIN) == LOW) {
		if(shutterState == CLOSING) {
			motorStop();
			shutterState = FINISHING_CLOSE;
		}
	}
}

void ShutterClass::OpenInterrupt()
{
	// debounce
	if (digitalRead(OPENED_PIN) == LOW) {
		if(shutterState == OPENING) {
			motorStop();
			shutterState = FINISHING_OPEN;
		}
	}
}

// EEPROM
void ShutterClass::SetDefaultConfig()
{
	memset(&m_Config, 0, sizeof(Configuration));
	m_Config.signature = EEPROM_SIGNATURE;
	m_Config.stepsPerStroke = 885000; // 368000
	m_Config.acceleration = 7000;
	m_Config.maxSpeed = 6400;
	m_Config.reversed = false;
	m_Config.cutoffVolts = 1150;
	m_Config.watchdogInterval = 90000;
	m_Config.panid = DEFAULT_PANID;
	m_Config.bHasDropShutter = false;
	m_Config.bTopShutterOpenFirst = true;
}

int ShutterClass::restoreDefaultMotorSettings()
{
	m_Config.stepsPerStroke = 885000; // 368000
	m_Config.acceleration = 7000;
	m_Config.maxSpeed = 6400;

	SetAcceleration(m_Config.acceleration);
	SetMaxSpeed(m_Config.maxSpeed);
	SetStepsPerStroke(m_Config.stepsPerStroke);
}


void ShutterClass::LoadFromEEProm()
{
	//  zero the structure so currently unused parts
	//  dont end up loaded with random garbage
	memset(&m_Config, 0, sizeof(Configuration));
#ifdef USE_EXT_EEPROM
	readEEPROMBuffer(EEPROM_ADDR, EEPROM_LOCATION, (byte *) &m_Config, sizeof(Configuration) );

#else
	byte* data = dueFlashStorage.readAddress(0);
	memcpy(&m_Config, data, sizeof(Configuration));
#endif

	DBPrintln("ShutterClass::LoadFromEEProm expected signature          : " + String(EEPROM_SIGNATURE));
	DBPrintln("ShutterClass::LoadFromEEProm m_Config.signature          : " + String(m_Config.signature));
	DBPrintln("ShutterClass::LoadFromEEProm m_Config.stepsPerStroke     : " + String(m_Config.stepsPerStroke));
	DBPrintln("ShutterClass::LoadFromEEProm m_Config.acceleration       : " + String(m_Config.acceleration));
	DBPrintln("ShutterClass::LoadFromEEProm m_Config.maxSpeed           : " + String(m_Config.maxSpeed));
	DBPrintln("ShutterClass::LoadFromEEProm m_Config.reversed           : " + String(m_Config.reversed?"Yes":"No"));
	DBPrintln("ShutterClass::LoadFromEEProm m_Config.cutoffVolts        : " + String(m_Config.cutoffVolts));
	DBPrintln("ShutterClass::LoadFromEEProm m_Config.watchdogInterval   : " + String(m_Config.watchdogInterval));
	DBPrintln("ShutterClass::LoadFromEEProm m_Config.panid              : 0x" + String(m_Config.panid, HEX));
	DBPrintln("ShutterClass::LoadFromEEProm m_Config.bHasDropShutter    : " + String(m_Config.bHasDropShutter?"Yes":"No"));
	DBPrintln("ShutterClass::LoadFromEEProm m_Config.bTopShutterOpenFirst   : " + String(m_Config.bTopShutterOpenFirst?"Yes":"No"));

	if (m_Config.signature != EEPROM_SIGNATURE) {
		SetDefaultConfig();
		SaveToEEProm();
		return;
	}

	if(m_Config.panid == 0x0000) {// this is not valid, something is wrong
		m_Config.panid = DEFAULT_PANID;
		SaveToEEProm();
	}
	if(m_Config.watchdogInterval > MAX_WATCHDOG_INTERVAL)
		m_Config.watchdogInterval = MAX_WATCHDOG_INTERVAL;
	if(m_Config.watchdogInterval < MIN_WATCHDOG_INTERVAL)
		m_Config.watchdogInterval = MIN_WATCHDOG_INTERVAL;


}

void ShutterClass::SaveToEEProm()
{

	DBPrintln("ShutterClass::SaveToEEProm : " + String(m_bDoEEPromSave?"Yes":"No"));
	if(!m_bDoEEPromSave)
		return;

	m_Config.signature = EEPROM_SIGNATURE;

#ifdef USE_EXT_EEPROM
	DBPrintln("Saving config to external AT24AA128 eeprom");
	writeEEPROM(EEPROM_ADDR, EEPROM_LOCATION, (byte *) &m_Config, sizeof(Configuration));
#else
	DBPrintln("Saving config to DUE flash");
	byte data[sizeof(Configuration)];
	memcpy(data, &m_Config, sizeof(Configuration));
	dueFlashStorage.write(0, data, sizeof(Configuration));
#endif

}

float ShutterClass::PositionToAltitude(const long pos)
{
	float result = (float)pos;
	result = result / m_Config.stepsPerStroke * 90.0;
	return result;
}

long ShutterClass::AltitudeToPosition(const float alt)
{
	long result;

	result = (long)(m_Config.stepsPerStroke * alt / 90.0);
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
	SaveToEEProm();
}

int ShutterClass::GetMaxSpeed()
{
	return stepper.maxSpeed();
}

void ShutterClass::SetMaxSpeed(const int speed)
{
	m_Config.maxSpeed = speed;
	stepper.setMaxSpeed(speed);
	SaveToEEProm();
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
	SaveToEEProm();
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
	SaveToEEProm();
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
	SaveToEEProm();
}

int ShutterClass::MeasureVoltage()
{
	int adc;
	float calc;

	adc = analogRead(VOLTAGE_MONITOR_PIN);
	calc = adc * m_fAdcConvert;
	DBPrintln("ADC volts = " + String(calc/100));
	return int(calc);
}

String ShutterClass::GetPANID()
{
	return String(m_Config.panid, HEX);
}

void ShutterClass::setPANID(const String panID)
{
	int newPanID = int(strtol(panID.c_str(), 0, 16));
	if(newPanID == 0)
		m_Config.panid = DEFAULT_PANID;
	else
		m_Config.panid = newPanID;

	SaveToEEProm();
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

	SaveToEEProm();
}

// INPUTS
void ShutterClass::DoButtons()
{
	int sw1, sw2, sw3, sw4;

	sw1 = digitalRead(BUTTON_OPEN);
	sw2 = digitalRead(BUTTON_CLOSE);

	sw3 = digitalRead(CLOSED_PIN);
	sw4 = digitalRead(OPENED_PIN);

	// roof is moving and the user want to stop it in the middle
	if((sw1 == LOW || sw2== LOW) && sw3 == HIGH && sw4 == HIGH && buttonStopTimer.elapsed() > 1.0 ) {
		motorStop();
		m_bUserButtonStop = true; // this allows us to not try to finish the open/close
		m_bButtonUsed = true;
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
		stopInterrupt();
	}
	else {
		digitalWrite(STEPPER_ENABLE_PIN, M_ENABLE);
	}
}

void ShutterClass::bufferEnable(bool bEnable)
{
	if(bEnable)
		digitalWrite(BUFFERN_EN, 0);
	else
		digitalWrite(BUFFERN_EN, 1);
}

// Movers
void ShutterClass::Open()
{
	m_nVolts = MeasureVoltage();
	if(GetVoltsAreLow()) // do not try to open if we're already at low voltage
		return;

	if (digitalRead(OPENED_PIN) == 0) {
		shutterState = OPEN;
		return;
	}

	shutterState = OPENING;
	DBPrintln("shutterState = OPENING");
	MoveRelative(160000000L);
}

void ShutterClass::Close()
{
	if (digitalRead(CLOSED_PIN) == 0) {
		shutterState = CLOSED;
		return;
	}
	shutterState = CLOSING;
	DBPrintln("shutterState = CLOSING");
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
		if(GetVoltsAreLow() && shutterState!=CLOSED) {
			DBPrintln("Voltage is low, closing");
			Close();
		}
		m_batteryCheckTimer.reset();
	}

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

void ShutterClass::stopInterrupt()
{
	// stop interrupt timer
	stopTimer(TC1, 0, TC3_IRQn);
}

void ShutterClass::motorMoveTo(const long newPosition)
{
	int nFreq;

	EnableMotor(true);
	stepper.moveTo(newPosition);
	// Why *3 .. I noticed that I don't do at least 3 times the amount of interrupt needed
	// AccelStepper doesn't work well
	nFreq = m_Config.maxSpeed *3 >20000 ? 20000 : m_Config.maxSpeed*3;

	// start interrupt timer
	// AccelStepper run() is called under a timer interrupt
	stopTimer(TC1, 0, TC3_IRQn);
	startTimer(TC1, 0, TC3_IRQn, nFreq);

}

void ShutterClass::motorMoveRelative(const long amount)
{
	int nFreq;

	EnableMotor(true);
	stepper.move(amount);
	// Why *3 .. I noticed that I don't do at least 3 times the amount of interrupt needed
	// AccelStepper doesn't work well
	nFreq = m_Config.maxSpeed *3 >20000 ? 20000 : m_Config.maxSpeed*3;
	// start interrupt timer
	// AccelStepper run() is called under a timer interrupt
	stopTimer(TC1, 0, TC3_IRQn);
	startTimer(TC1, 0, TC3_IRQn, nFreq);
}



#ifdef USE_EXT_EEPROM

//
// EEProm code to access the AT24AA128 I2C eeprom
//

// read one byte
byte ShutterClass::readEEPROMByte(int deviceaddress, unsigned int eeaddress)
{
	byte rdata = 0xFF;

	Wire1.beginTransmission(deviceaddress);
	Wire1.write((int)(eeaddress >> 8)); // MSB
	Wire1.write((int)(eeaddress & 0xFF)); // LSB
	Wire1.endTransmission();
	Wire1.requestFrom(deviceaddress,1);
	if (Wire1.available()) {
		rdata = Wire1.read();
	}
	return rdata;
}

// Read from EEPROM into a buffer
// slice read into I2C_CHUNK_SIZE block read. I2C_CHUNK_SIZE <=16
void ShutterClass::readEEPROMBuffer(int deviceaddress, unsigned int eeaddress, byte *buffer, int length)
{

	int c = length;
	int offD = 0;
	int nc = 0;

	// read until length bytes is read
	while (c > 0) {
		// read maximal I2C_CHUNK_SIZE bytes
		nc = c;
		if (nc > I2C_CHUNK_SIZE)
			nc = I2C_CHUNK_SIZE;
		readEEPROMBlock(deviceaddress, eeaddress, buffer, offD, nc);
		eeaddress+=nc;
		offD+=nc;
		c-=nc;
	}
}

// Read from eeprom into a buffer  (assuming read lenght if I2C_CHUNK_SIZE or less)
void ShutterClass::readEEPROMBlock(int deviceaddress, unsigned int eeaddress, byte *data, int offset, int length)
{
	int r = 0;
	Wire1.beginTransmission(deviceaddress);
	if (Wire1.endTransmission()==0) {
	 	Wire1.beginTransmission(deviceaddress);
		Wire1.write(eeaddress >> 8);
		Wire1.write(eeaddress & 0xFF);
		if (Wire1.endTransmission()==0) {
			r = 0;
			Wire1.requestFrom(deviceaddress, length);
			while (Wire1.available() > 0 && r<length) {
				data[offset+r] = (byte)Wire1.read();
				r++;
			}
		}
	}
}



// Write a buffer to EEPROM
// slice write into CHUNK_SIZE block write. I2C_CHUNK_SIZE <=16
void ShutterClass::writeEEPROM(int deviceaddress, unsigned int eeaddress, byte *data, int length)
{
	int c = length;					// bytes left to write
	int offD = 0;					// current offset in data pointer
	int offP;						// current offset in page
	int nc = 0;						// next n bytes to write

	// write all bytes in multiple steps
	while (c > 0) {
		// calc offset in page
		offP = eeaddress % m_EEPROMpageSize;
		// maximal I2C_CHUNK_SIZE bytes to write
		nc = min(min(c, I2C_CHUNK_SIZE), m_EEPROMpageSize - offP);
		writeEEPROMBlock(deviceaddress, eeaddress, data, offD, nc);
		c-=nc;
		offD+=nc;
		eeaddress+=nc;
	}
}

// Write a buffer to EEPROM
void ShutterClass::writeEEPROMBlock(int deviceaddress, unsigned int eeaddress, byte *data, int offset, int length)
{

	Wire1.beginTransmission(deviceaddress);
	if (Wire1.endTransmission()==0) {
	 	Wire1.beginTransmission(deviceaddress);
		Wire1.write(eeaddress >> 8);
		Wire1.write(eeaddress & 0xFF);
		byte *adr = data+offset;
		Wire1.write(adr, length);
		Wire1.endTransmission();
		delay(20);
	} else {
		DBPrintln("No device at address 0x" + String(deviceaddress, HEX));
	}
}

#endif



