//
// RTI-Zone Dome Shutter firmware. Based on https://github.com/nexdome/Automation/tree/master/Firmwares
// As I contributed a lot to it and it's being deprecated, I'm now using it for myself.
//


#if defined ARDUINO_DUE
#include <DueFlashStorage.h>
DueFlashStorage dueFlashStorage;
#else
#include <EEPROM.h>
#endif

#include <AccelStepper.h>

#include "StopWatch.h"

// Debug printing, uncomment #define DEBUG to enable
#define DEBUG
#ifdef DEBUG
#define DBPrint(x) DebugPort.print(x)
#define DBPrintln(x) DebugPort.println(x)
#else
#define DBPrint(x)
#define DBPrintln(x)
#endif // DEBUG

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

#define     EEPROM_LOCATION        100  // not used with Arduino Due
#define     EEPROM_SIGNATURE      2642

#define MIN_WATCHDOG_INTERVAL    60000
#define MAX_WATCHDOG_INTERVAL   300000

#define VOLTAGE_MONITOR_PIN A0
#if defined(ARDUINO_DUE)
#define AD_REF      3.3
#define RES_MULT    5.0 // resistor voltage divider on the shield
#else
#define AD_REF      5.0
#define RES_MULT    3.0 // resistor voltage divider on the shield
#endif

#if defined(TB6600)
#define M_ENABLE    LOW
#define M_DISABLE   HIGH
#elif defined(ISD0X)
#define M_ENABLE    HIGH
#define M_DISABLE   LOW
#else
#define M_ENABLE    LOW
#define M_DISABLE   HIGH
#endif

typedef struct ShutterConfiguration {
    int             signature;
    unsigned long   stepsPerStroke;
    int             acceleration;
    int             maxSpeed;
    bool            reversed;
    int             cutoffVolts;
    int             voltsClose;
    unsigned long   watchdogInterval;
    bool            radioIsConfigured;
    int             panid;
} Configuration;


AccelStepper stepper(AccelStepper::DRIVER, STEPPER_STEP_PIN, STEPPER_DIRECTION_PIN);

// need to make this global so we can access it in the interrupt
enum ShutterStates { OPEN, CLOSED, OPENING, CLOSING, ERROR };
ShutterStates   shutterState = ERROR;

StopWatch watchdogTimer;


#if defined ARDUINO_DUE
/*
 * As demonstrated by RCArduino and modified by BKM:
 * pick clock that provides the least error for specified frequency.
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
#endif


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
    int         GetVoltsClose();
    void        SetVoltsClose(const int);
    void        SetVoltsFromString(const String);

    String      GetPANID();
    void        setPANID(const String panID);
    bool        isRadioConfigured();
    void        setRadioConfigured(bool bConfigured);

    unsigned long   getWatchdogInterval();
    void            SetWatchdogInterval(const unsigned long);

    // Move
    bool        DoButtons();
    void        EnableMotor(const bool);
    void        Open();
    void        Close();
    void        Run();
    static void motorStop();
    void        motorMoveTo(const long newPosition);
    void        motorMoveRelative(const long amount);
#if defined ARDUINO_DUE
    void        stopInterrupt();
#endif

    // persistent data
    void        LoadFromEEProm();
    void        SaveToEEProm();
    int         restoreDefaultMotorSettings();

    // interrupts
    static void     ClosedInterrupt();
    static void     OpenInterrupt();

private:

    Configuration   m_Config;
    float           m_fAdcConvert;
    int             m_nVolts;
    StopWatch       m_batteryCheckTimer;
    unsigned long   m_nBatteryCheckInterval = 0; // we want to check battery immedialtelly
    // int             m_nLastButtonPressed;

    int             MeasureVoltage();
    void            SetDefaultConfig();
};


ShutterClass::ShutterClass()
{
    int sw1, sw2;
    m_fAdcConvert = RES_MULT * (AD_REF / 1024.0) * 100;

    pinMode(CLOSED_PIN, INPUT_PULLUP);
    pinMode(OPENED_PIN, INPUT_PULLUP);
    pinMode(STEPPER_STEP_PIN, OUTPUT);
    pinMode(STEPPER_DIRECTION_PIN, OUTPUT);
    pinMode(STEPPER_ENABLE_PIN, OUTPUT);
    pinMode(BUTTON_OPEN, INPUT_PULLUP);
    pinMode(BUTTON_CLOSE, INPUT_PULLUP);
    pinMode(VOLTAGE_MONITOR_PIN, INPUT);
    LoadFromEEProm();
    stepper.setEnablePin(STEPPER_ENABLE_PIN);
    SetAcceleration(m_Config.acceleration);
    SetMaxSpeed(m_Config.maxSpeed);
    EnableMotor(false);
    attachInterrupt(digitalPinToInterrupt(CLOSED_PIN), ClosedInterrupt, FALLING);
    attachInterrupt(digitalPinToInterrupt(OPENED_PIN), OpenInterrupt, FALLING);

    // reset all timers
    m_batteryCheckTimer.reset();

    // read initial shutter state
    sw1 = digitalRead(CLOSED_PIN);
    sw2 = digitalRead(OPENED_PIN);

    if(sw1 == 0 && sw2 == 0)
        shutterState = ERROR;
    else if (sw1 == 1 && sw2 == 0)
        shutterState = CLOSED;
    else if (sw1 == 0 && sw2 == 1)
        shutterState = OPEN;

}

void ShutterClass::ClosedInterrupt()
{
    // debounce
    if (digitalRead(CLOSED_PIN) == 0) {
        if(shutterState == CLOSING)
            ShutterClass::motorStop();
        if(shutterState != OPENING)
            shutterState = CLOSED;
    }
}

void ShutterClass::OpenInterrupt()
{
    // debounce
    if (digitalRead(OPENED_PIN) == 0) {
        if(shutterState == OPENING)
            ShutterClass::motorStop();
        if(shutterState != CLOSING)
            shutterState = OPEN;
    }
}

// EEPROM
void ShutterClass::SetDefaultConfig()
{
    memset(&m_Config, 0, sizeof(Configuration));
    m_Config.signature = EEPROM_SIGNATURE;
    m_Config.stepsPerStroke = 885000;
    m_Config.acceleration = 7000;
    m_Config.maxSpeed = 5000;
    m_Config.reversed = false;
    m_Config.cutoffVolts = 1150;
    m_Config.voltsClose = 0;
    m_Config.watchdogInterval = 90000;
    m_Config.radioIsConfigured = false;
    m_Config.panid = 0x4242;
}

int ShutterClass::restoreDefaultMotorSettings()
{
    m_Config.stepsPerStroke = 885000;
    m_Config.acceleration = 7000;
    m_Config.maxSpeed = 5000;

    SetAcceleration(m_Config.acceleration);
    SetMaxSpeed(m_Config.maxSpeed);
    SetStepsPerStroke(m_Config.stepsPerStroke);
}


void ShutterClass::LoadFromEEProm()
{
#if defined ARDUINO_DUE // DUE
    byte* data = dueFlashStorage.readAddress(0);
    memcpy(&m_Config, data, sizeof(Configuration));
#else
    EEPROM.get(EEPROM_LOCATION, m_Config);
#endif
    if (m_Config.signature != EEPROM_SIGNATURE) {
        SetDefaultConfig();
        SaveToEEProm();
        return;
    }

    if(m_Config.watchdogInterval > MAX_WATCHDOG_INTERVAL)
        m_Config.watchdogInterval = MAX_WATCHDOG_INTERVAL;
    if(m_Config.watchdogInterval < MIN_WATCHDOG_INTERVAL)
        m_Config.watchdogInterval = MIN_WATCHDOG_INTERVAL;
}

void ShutterClass::SaveToEEProm()
{

#if defined ARDUINO_DUE // DUE
    byte data[sizeof(Configuration)];
    memcpy(data, &m_Config, sizeof(Configuration));
    dueFlashStorage.write(0, data, sizeof(Configuration));
#else
    EEPROM.put(EEPROM_LOCATION, m_Config);
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

    if (digitalRead(CLOSED_PIN) == 0)
        result = CLOSED;

    if (digitalRead(OPENED_PIN) == 0)
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
    bool low = (m_nVolts <= m_Config.cutoffVolts);
    return low;
}

String ShutterClass::GetVoltString()
{
    return String(m_nVolts) + "," + String(m_Config.cutoffVolts);
}

inline int ShutterClass::GetVoltsClose()
{
    return m_Config.voltsClose;
}

inline void ShutterClass::SetVoltsClose(const int value)
{
    m_Config.voltsClose = value;
    SaveToEEProm();
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
    DBPrintln("ADC returns " + String(adc));
    calc = adc * m_fAdcConvert;
    return int(calc);
}

String ShutterClass::GetPANID()
{
    return String(m_Config.panid, HEX);
}

void ShutterClass::setPANID(const String panID)
{
    m_Config.panid = strtol(panID.c_str(), 0, 16);
    SaveToEEProm();
}

bool ShutterClass::isRadioConfigured()
{
    return m_Config.radioIsConfigured;
}

void ShutterClass::setRadioConfigured(bool bConfigured)
{
    m_Config.radioIsConfigured = bConfigured;
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
bool ShutterClass::DoButtons()
{
    int PRESSED = 0;
    bool bButtonUsed = false;
    static int whichButtonPressed = 0, lastButtonPressed = 0;

    if (digitalRead(BUTTON_OPEN) == PRESSED && whichButtonPressed == 0 && GetEndSwitchStatus() != OPEN) {
        DBPrintln("Button Open Shutter");
        watchdogTimer.reset();
        whichButtonPressed = BUTTON_OPEN;
        shutterState = OPENING;
        MoveRelative(m_Config.stepsPerStroke);
        lastButtonPressed = BUTTON_OPEN;
        bButtonUsed = true;
    }
    else if (digitalRead(BUTTON_CLOSE) == PRESSED && whichButtonPressed == 0 && GetEndSwitchStatus() != CLOSED) {
        DBPrintln("Button Close Shutter");
        watchdogTimer.reset();
        whichButtonPressed = BUTTON_CLOSE;
        shutterState = CLOSING;
        MoveRelative(1 - m_Config.stepsPerStroke);
        lastButtonPressed = BUTTON_CLOSE;
        bButtonUsed = true;
    }

    if (digitalRead(whichButtonPressed) == !PRESSED && lastButtonPressed > 0) {
        motorStop();
        lastButtonPressed = whichButtonPressed = 0;
        bButtonUsed = false;
    }

    return bButtonUsed;
}

// Setters
void ShutterClass::EnableMotor(const bool newState)
{
    if (!newState) {
        digitalWrite(STEPPER_ENABLE_PIN, M_DISABLE);
#if defined ARDUINO_DUE
        stopInterrupt();
#endif
    }
    else {
        digitalWrite(STEPPER_ENABLE_PIN, M_ENABLE);
    }
}

// Movers
void ShutterClass::Open()
{
    shutterState = OPENING;
    DBPrintln("shutterState = OPENING");
    MoveRelative(m_Config.stepsPerStroke * 1.2);
}

void ShutterClass::Close()
{
    shutterState = CLOSING;
    DBPrintln("shutterState = CLOSING");
    MoveRelative(1 - m_Config.stepsPerStroke * 1.2);
}

void ShutterClass::Run()
{
    static bool hitSwitch = false, firstBatteryCheck = true, doSync = true;

#ifndef ARDUINO_DUE
    stepper.run(); // we don't want the stepper to stop
#endif

    // ADC reads low if you sample too soon after startup
    // and battery check interval of 5 minutes means no accurate
    // display in ASCOM until after five minutes. So this first
    // delay should be late enough for accurate ADC reading but
    // fast enough to be available in ASCOM when the setup window
    // is opened.
    // Make both switches effectively one circuit so DIYers can use just one circuit
    // Determines opened or closed by the direction of travel before a switch was hit

    if (digitalRead(CLOSED_PIN) == 0 && shutterState != OPENING && hitSwitch == false) {
            hitSwitch = true;
            doSync = true;
            shutterState = CLOSED;
            motorStop();
            DBPrintln("Hit closed switch");
            DBPrintln("shutterState = CLOSED");
    }

    if (digitalRead(OPENED_PIN) == 0 && shutterState != CLOSING && hitSwitch == false) {
            hitSwitch = true;
            shutterState = OPEN;
            motorStop();
            DBPrintln("Hit opened switch");
            DBPrintln("shutterState = OPEN");
    }

    if (stepper.isRunning()) {
        m_bWasRunning = true;
    }

    if (stepper.isRunning() == false && digitalRead(OPENED_PIN) != 0 && digitalRead(CLOSED_PIN) != 0) {
        shutterState = ERROR;
    }

    if (m_batteryCheckTimer.elapsed() >= m_nBatteryCheckInterval) {
        DBPrintln("Measuring Battery");
        m_nVolts = MeasureVoltage();
        if (firstBatteryCheck) {
            m_batteryCheckTimer.reset();
            m_nBatteryCheckInterval  = 5000;
            firstBatteryCheck = false;
        }
        else {
            m_batteryCheckTimer.reset();
            m_nBatteryCheckInterval = 120000;
        }
    }

    if (stepper.isRunning())
        return;

    if (doSync && digitalRead(CLOSED_PIN) == 0) {
            stepper.setCurrentPosition(0);
            doSync = false;
            DBPrintln("Stopped at closed position");
    }

    if (m_bWasRunning) { // So this bit only runs once after stopping.
        DBPrintln("m_bWasRunning " + String(shutterState) + " Hitswitch " + String(hitSwitch));
        // m_nLastButtonPressed = 0;
        m_bWasRunning = false;
        hitSwitch = false;
        EnableMotor(false);
    }
}


void ShutterClass::motorStop()
{
    stepper.stop();

}

#if defined ARDUINO_DUE
void ShutterClass::stopInterrupt()
{
    // stop interrupt timer
    stopTimer(TC1, 0, TC3_IRQn);
}
#endif

void ShutterClass::motorMoveTo(const long newPosition)
{
    EnableMotor(true);
    stepper.moveTo(newPosition);
#if defined ARDUINO_DUE
    int nFreq;
    nFreq = m_Config.maxSpeed *3 >20000 ? 20000 : m_Config.maxSpeed*3;
    // start interrupt timer
    // AccelStepper run() is called under a timer interrupt
    startTimer(TC1, 0, TC3_IRQn, nFreq);
#endif

}

void ShutterClass::motorMoveRelative(const long amount)
{

    EnableMotor(true);
    stepper.move(amount);
#if defined ARDUINO_DUE
    int nFreq;
    nFreq = m_Config.maxSpeed *3 >20000 ? 20000 : m_Config.maxSpeed*3;
    // start interrupt timer
    // AccelStepper run() is called under a timer interrupt
    startTimer(TC1, 0, TC3_IRQn, nFreq);
#endif
}
