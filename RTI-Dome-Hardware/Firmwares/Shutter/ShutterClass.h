//
// RTI-Zone Dome Shutter firmware. Based on https://github.com/nexdome/Automation/tree/master/Firmwares
// As I contributed a lot to it and it's being deprecated, I'm now using it for myself.
//



#if defined(ARDUINO) && ARDUINO >= 100
#include "arduino.h"
#else
#include "WProgram.h"
#endif
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
#ifdef TEENY_3_5
#define     STEPPER_ENABLE_PIN       9
#define     STEPPER_DIRECTION_PIN   10
#define     STEPPER_STEP_PIN         6
#define     CLOSED_PIN               4
#define     OPENED_PIN               5
#define     BUTTON_OPEN              7
#define     BUTTON_CLOSE             8
#else   // Standard Arduino
#define     STEPPER_ENABLE_PIN      10
#define     STEPPER_DIRECTION_PIN   11
#define     STEPPER_STEP_PIN        12
#define     CLOSED_PIN               2
#define     OPENED_PIN               3
#define     BUTTON_OPEN              5
#define     BUTTON_CLOSE             6
#endif


#define     EEPROM_LOCATION        100
#define     EEPROM_SIGNATURE      2640

#define MIN_WATCHDOG_INTERVAL    60000
#define MAX_WATCHDOG_INTERVAL   300000

#define VOLTAGE_MONITOR_PIN A0
#if defined(ARDUINO_DUE)
#define AD_REF  3.3
#else
#define AD_REF  5.0
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
    uint64_t        stepsPerStroke;
    uint16_t        acceleration;
    uint16_t        maxSpeed;
    uint8_t         reversed;
    uint16_t        cutoffVolts;
    byte            voltsClose;
    unsigned long   watchdogInterval;
    bool            radioIsConfigured;
    int             panid;
} Configuration;



AccelStepper stepper(AccelStepper::DRIVER, STEPPER_STEP_PIN, STEPPER_DIRECTION_PIN);

// need to make this global so we can access it in the interrupt
enum ShutterStates { OPEN, CLOSED, OPENING, CLOSING, ERROR };
ShutterStates   shutterState = ERROR;

StopWatch watchdogTimer;

class ShutterClass
{
public:

    // Constructor
    ShutterClass();

    bool        m_bWasRunning = false;

    // Helper functions
    float       PositionToAltitude(long);
    long        AltitudeToPosition(float);

    // Getters
    int32_t     GetAcceleration();
    float       GetElevation();
    int         GetEndSwitchStatus();
    uint32_t    GetMaxSpeed();
    long        GetPosition();
    bool        GetReversed();
    short       GetState();
    uint32_t    GetStepsPerStroke();
    bool        GetVoltsAreLow();
    String      GetVoltString();
    String      GetPANID();
    bool        isRadioConfigured();
    int         getWatchdogInterval();
    // Setters
    void        SetAcceleration(const uint16_t);
    void        SetMaxSpeed(const uint16_t);
    void        SetReversed(const bool);
    void        SetStepsPerStroke(const uint32_t);
    void        SetVoltsFromString(const String);
    void        setPANID(const String panID);
    void        setRadioConfigured(bool bConfigured);
    void        setWatchdogInterval(int nIntervalMs);
    // Movers
    bool        DoButtons();
    void        Open();
    void        Close();
    void        GotoPosition(const unsigned long);
    void        GotoAltitude(const float);
    void        MoveRelative(const long);
    void        SetWatchdogInterval(const unsigned long);
    byte        GetVoltsClose();
    void        SetVoltsClose(const byte);

    // xbee stuff
    void        EnableMotor(const bool);
    void        Run();
    void        Stop();
    void        LoadFromEEProm();
    void        SaveToEEProm();
    int         restoreDefaultMotorSettings();

    static void     ClosedInterrupt();
    static void     OpenInterrupt();

private:

    Configuration m_Config;

    float           m_fAdcConvert;
    uint16_t        m_nVolts;
    StopWatch       m_batteryCheckTimer;
    unsigned long   m_nBatteryCheckInterval = 0; // we want to check battery immedialtelly

    uint8_t         m_nLastButtonPressed;


    int         MeasureVoltage();
    void        SetDefaultConfig();
};



ShutterClass::ShutterClass()
{
    int sw1, sw2;
    m_fAdcConvert = 5.0 * (AD_REF / 1024.0) * 100;

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
            stepper.stop();
        if(shutterState != OPENING)
            shutterState = CLOSED;
    }
}

void ShutterClass::OpenInterrupt()
{
    // debounce
    if (digitalRead(OPENED_PIN) == 0) {
        if(shutterState == OPENING)
            stepper.stop();
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
        Stop();
        lastButtonPressed = whichButtonPressed = 0;
        bButtonUsed = false;
    }

    return bButtonUsed;
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

// Helper functions
long ShutterClass::AltitudeToPosition(const float alt)
{
    long result;

    result = (long)(m_Config.stepsPerStroke * alt / 90.0);
    return result;
}

float ShutterClass::PositionToAltitude(const long pos)
{
    float result = (float)pos;
    result = result / m_Config.stepsPerStroke * 90.0;
    return result;
}

// Wireless Functions

// Getters
int32_t ShutterClass::GetAcceleration()
{
    return m_Config.acceleration;
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

float ShutterClass::GetElevation()
{
    return PositionToAltitude(stepper.currentPosition());
}

uint32_t ShutterClass::GetMaxSpeed()
{
    return stepper.maxSpeed();
}

long ShutterClass::GetPosition()
{
    return stepper.currentPosition();
}

bool ShutterClass::GetReversed()
{
    return m_Config.reversed;
}

short ShutterClass::GetState()
{
    return (short)shutterState;
}

uint32_t ShutterClass::GetStepsPerStroke()
{
    return m_Config.stepsPerStroke;
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

void ShutterClass::SetAcceleration(const uint16_t accel)
{
    m_Config.acceleration = accel;
    stepper.setAcceleration(accel);
    SaveToEEProm();
}

void ShutterClass::SetMaxSpeed(const uint16_t speed)
{
    m_Config.maxSpeed = speed;
    stepper.setMaxSpeed(speed);
    SaveToEEProm();
}

void ShutterClass::SetReversed(const bool reversed)
{
    m_Config.reversed = reversed;
    stepper.setPinsInverted(reversed, reversed, reversed);
    SaveToEEProm();
}

void ShutterClass::SetStepsPerStroke(const uint32_t newSteps)
{
    m_Config.stepsPerStroke = newSteps;
    SaveToEEProm();
}

void ShutterClass::SetVoltsFromString(const String value)
{
    m_Config.cutoffVolts = value.toInt();
    SaveToEEProm();
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
        EnableMotor(true);
        stepper.moveTo(newPos);
    }
}

void ShutterClass::GotoAltitude(const float newAlt)
{

    GotoPosition(AltitudeToPosition(newAlt));
}

void ShutterClass::MoveRelative(const long amount)
{
    EnableMotor(true);
    stepper.move(amount);
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

inline void ShutterClass::SetVoltsClose(const byte value)
{
    m_Config.voltsClose = value;
    SaveToEEProm();
}

inline byte ShutterClass::GetVoltsClose()
{
    return m_Config.voltsClose;
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


int ShutterClass::getWatchdogInterval()
{
    return m_Config.watchdogInterval;
}

void ShutterClass::setWatchdogInterval(int nIntervalMs)
{
    m_Config.watchdogInterval = nIntervalMs;
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
            stepper.stop();
            DBPrintln("Hit closed switch");
            DBPrintln("shutterState = CLOSED");
    }

    if (digitalRead(OPENED_PIN) == 0 && shutterState != CLOSING && hitSwitch == false) {
            hitSwitch = true;
            shutterState = OPEN;
            stepper.stop();
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
        m_nLastButtonPressed = 0;
        m_bWasRunning = false;
        hitSwitch = false;
        EnableMotor(false);
    }
}

void        ShutterClass::Stop()
{
    stepper.stop();
}

