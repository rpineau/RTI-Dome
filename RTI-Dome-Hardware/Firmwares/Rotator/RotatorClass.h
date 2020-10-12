//
// RTI-Zone Dome Rotator firmware. Based on https://github.com/nexdome/Automation/tree/master/Firmwares
// As I contributed a lot to it and it's being deprecated, I'm now using it for myself.
//


#if defined(ARDUINO) && ARDUINO >= 100
#include "arduino.h"
#else
#include "WProgram.h"
#endif

#if defined ARDUINO_DUE // DUE
#include <DueFlashStorage.h>
DueFlashStorage dueFlashStorage;
#else
#include <EEPROM.h>
#endif

#include <AccelStepper.h>
#include "StopWatch.h"

// set this to match the type of steps configured on the
// stepper controller
#define STEP_TYPE 8

// #define DEBUG
#ifdef DEBUG
#define DBPrint(x) DebugPort.println(x)
#else
#define DBPrint(x)
#endif // DEBUG


#ifndef TEENY_3_5
// Arduino boards
#define HOME_PIN             2  // Also used for Shutter open status
#define STEPPER_ENABLE_PIN  10  // Digital Output
#define DIRECTION_PIN       11  // Digital Output
#define STEP_PIN            12  // Digital Output
#define BUTTON_CCW           5  // Digital Input
#define BUTTON_CW            6  // Digital Input
#define RAIN_SENSOR_PIN      7  // Digital Input from RG11
#else
// Teensy boards > 3.5
#define HOME_PIN             4  // Also used for Shutter open status
#define STEPPER_ENABLE_PIN   9  // Digital Output
#define DIRECTION_PIN       10  // Digital Output
#define STEP_PIN             6  // Digital Output
#define BUTTON_CCW           7  // Digital Input
#define BUTTON_CW            8  // Digital Input
#define RAIN_SENSOR_PIN     19  // Digital Input from RG11
#endif

#define VOLTAGE_MONITOR_PIN A0
#if defined(ARDUINO_DUE)
#define AD_REF      3.3
#define RES_MULT    5.0 // resistor voltage divider on the shield
#else
#define AD_REF      5.0
#define RES_MULT    3.0 // resistor voltage divider on the shield
#endif


#define MOVE_NEGATIVE       -1
#define MOVE_NONE            0
#define MOVE_POSITIVE        1

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


#ifndef STANDALONE
#define SIGNATURE         2641
#else
#define SIGNATURE         2641
#endif
// not used on DUE
#define EEPROM_LOCATION     10


typedef struct RotatorConfiguration {
    int             signature;
    long            maxSpeed;
    long            acceleration;
    long            stepsPerRotation;
    bool            reversed;
    float           homeAzimuth;
    float           parkAzimuth;
    int             cutOffVolts;
    unsigned long   rainCheckInterval;
    bool            rainCheckTwice;
    byte            rainAction;
#ifndef STANDALONE
    bool            radioIsConfigured;
    int             panid;
#endif
} Configuration;

enum HomeStatuses { NEVER_HOMED, HOMED, ATHOME };
enum Seeks { HOMING_NONE, // Not homing or calibrating
                HOMING_HOME, // Homing
                CALIBRATION_MOVEOFF, // Ignore home until we've moved off while measuring the dome.
                CALIBRATION_MEASURE // Measuring dome until home hit again.
};

enum RainActions {DO_NOTHING=0, HOME, PARK};


AccelStepper stepper(AccelStepper::DRIVER, STEP_PIN, DIRECTION_PIN);


class RotatorClass
{

public:

    RotatorClass();

    // Getters
    bool        GetRainStatus();
    int         GetLowVoltageCutoff();
    bool        GetVoltsAreLow();
    String      GetVoltString();
    long        GetPosition();
    int         GetSeekMode();
    int         GetDirection();
    long        GetMaxSpeed();
    long        GetAcceleration();
    long        GetStepsPerRotation();
    byte        GetRainAction();
    unsigned long           GetRainCheckInterval();
    bool        GetRainCheckTwice();
    bool        GetReversed();
    float       GetAzimuth();
    float       GetHomeAzimuth();
    int         GetHomeStatus();

    float       GetParkAzimuth();
    long        GetAzimuthToPosition(const float);
#ifndef STANDALONE
    String      GetPANID();
    bool        isRadioConfigured();
#endif

    // Setters
    void        SetLowVoltageCutoff(const int);
    void        SetPosition(const long);
    void        SetMaxSpeed(const long);
    void        SetAcceleration(const long);
    void        GoToAzimuth(const float);
    void        SetParkAzimuth(const float);
    void        SetStepsPerRotation(const long);
    void        SetRainInterval(const unsigned long);
    void        SetReversed(const bool reversed);
    void        SetHomeAzimuth(const float);
    void        SetRainAction(const byte);
    void        SetCheckRainTwice(const bool);
    void        SetHomingCalibratingSpeed(const long newSpeed);
    void        RestoreNormalSpeed();
#ifndef STANDALONE
    void        setPANID(const String panID);
    void        setRadioConfigured(bool bConfigured);
#endif
    // Movers
    void        MoveRelative(const long steps);
    void        Stop();
    void        Run();


    // Homing and Calibration
    void        StartHoming();
    void        StartCalibrating();
    void        Calibrate();
    void        SyncPosition(const float);
    void        SyncHome(const float);
    void        SaveToEEProm();

    void        EnableMotor(const bool);
    int         restoreDefaultMotorSettings();

private:

    Configuration   m_Config;

    // Inputs
    int         m_nBtnCCWPin;
    int         m_nBtnCWpin;
    int         m_nRainPin;

    // Rotator
    bool        m_bisAtHome;
    bool        m_bHasBeenHomed;
    enum Seeks  m_seekMode;
    bool        m_bSetToHomeAzimuth, m_bDoStepsPerRotation;
    long        m_nStepsToStop;
    float       m_fStepsPerDegree;
    StopWatch   m_moveOffUntilTimer;
    unsigned long m_nMoveOffUntilLapse = 5000;
    unsigned long m_nNextCheckLapse = 10000;

    int         m_nMoveDirection;

    // Power values
    float       m_fAdcConvert;
    int         m_nVolts;
    int         ReadVolts();


    StopWatch       m_periodicReadingTimer;
    unsigned long   m_nNextPeriodicReadingLapse = 10;


    // Utility
    StopWatch       m_buttonCheckTimer;
    unsigned long   m_nNextCheckButtonLapse = 10;
    void            ButtonCheck();

    bool            LoadFromEEProm();
    void            SetDefaultConfig();

    float       GetAngularDistance(const float fromAngle, const float toAngle);

};

RotatorClass::RotatorClass()
{
    m_fAdcConvert = RES_MULT * (AD_REF / 1024.0) * 100;

    LoadFromEEProm();
    pinMode(HOME_PIN, INPUT_PULLUP);
    pinMode(STEP_PIN, OUTPUT);
    pinMode(DIRECTION_PIN, OUTPUT);
    pinMode(STEPPER_ENABLE_PIN, OUTPUT);
    pinMode(BUTTON_CCW, INPUT_PULLUP);
    pinMode(BUTTON_CW, INPUT_PULLUP);
    pinMode(RAIN_SENSOR_PIN, INPUT_PULLUP);
    pinMode(VOLTAGE_MONITOR_PIN, INPUT);

    // reset all timers
    m_moveOffUntilTimer.reset();
    m_periodicReadingTimer.reset();

}

void RotatorClass::SaveToEEProm()
{
    m_Config.signature = SIGNATURE;

#if defined ARDUINO_DUE // DUE
    byte data[sizeof(Configuration)];
    memcpy(data, &m_Config, sizeof(Configuration));
    dueFlashStorage.write(0, data, sizeof(Configuration));
#else
    EEPROM.put(EEPROM_LOCATION, m_Config);
#endif
}

bool RotatorClass::LoadFromEEProm()
{
    bool response = true;

    //  zero the structure so currently unused parts
    //  dont end up loaded with random garbage
    memset(&m_Config, 0, sizeof(Configuration));

#if defined ARDUINO_DUE // DUE
    byte* data = dueFlashStorage.readAddress(0);
    memcpy(&m_Config, data, sizeof(Configuration));
#else
    EEPROM.get(EEPROM_LOCATION, m_Config);
#endif

    if (m_Config.signature != SIGNATURE) {
        SetDefaultConfig();
        SaveToEEProm();
        response = false;
    }

    SetMaxSpeed(m_Config.maxSpeed);
    SetAcceleration(m_Config.acceleration);
    SetStepsPerRotation(m_Config.stepsPerRotation);
    SetReversed(m_Config.reversed);
#ifndef STANDALONE
    if(m_Config.panid < 0) { // set to default.. there was something bad in eeprom.
        SetDefaultConfig();
        SaveToEEProm();
        response = false;
    }
#endif
    return response;
}

void RotatorClass::SetDefaultConfig()
{
    memset(&m_Config, 0, sizeof(Configuration));

    m_Config.signature = SIGNATURE;
    m_Config.maxSpeed = 5000;
    m_Config.acceleration = 8000;
    m_Config.stepsPerRotation = 440800;
    m_Config.reversed = 0;
    m_Config.homeAzimuth = 0;
    m_Config.parkAzimuth = 0;
    m_Config.cutOffVolts = 1150;
    m_Config.rainCheckInterval = 10; // In seconds
    m_Config.rainCheckTwice = false;
    m_Config.rainAction = DO_NOTHING;
#ifndef STANDALONE
    m_Config.radioIsConfigured = false;
    m_Config.panid = 0x4242;
#endif
}

int RotatorClass::restoreDefaultMotorSettings()
{
    m_Config.maxSpeed = 5000;
    m_Config.acceleration = 8000;
    m_Config.stepsPerRotation = 440800;
    SetMaxSpeed(m_Config.maxSpeed);
    SetAcceleration(m_Config.acceleration);
    SetStepsPerRotation(m_Config.stepsPerRotation);
}

int RotatorClass::ReadVolts()
{
    int adc;
    float calc;

    adc = analogRead(VOLTAGE_MONITOR_PIN);
    calc = adc * m_fAdcConvert;
    return int(calc);
}

void RotatorClass::SetLowVoltageCutoff(const int lowVolts)
{
    m_Config.cutOffVolts = lowVolts;
    SaveToEEProm();
}

int RotatorClass::GetLowVoltageCutoff()
{
    return m_Config.cutOffVolts;
}

inline bool RotatorClass::GetVoltsAreLow()
{
    bool voltsLow = false;

    if (m_nVolts <= m_Config.cutOffVolts)
        voltsLow = true;
    return voltsLow;
}

inline String RotatorClass::GetVoltString()
{
    return String(m_nVolts) + "," + String(m_Config.cutOffVolts);
}

void RotatorClass::ButtonCheck()
{
    int PRESSED = 0;
    static int whichButtonPressed = 0, lastButtonPressed = 0;

    if (digitalRead(BUTTON_CW) == PRESSED && whichButtonPressed == 0) {
        whichButtonPressed = BUTTON_CW;
        MoveRelative(m_Config.stepsPerRotation);
        lastButtonPressed = BUTTON_CW;
    }
    else if (digitalRead(BUTTON_CCW) == PRESSED && whichButtonPressed == 0) {
        whichButtonPressed = BUTTON_CCW;
        MoveRelative(1 - m_Config.stepsPerRotation);
        lastButtonPressed = BUTTON_CCW;
    }

    if (digitalRead(whichButtonPressed) == !PRESSED && lastButtonPressed > 0) {
        Stop();
        lastButtonPressed = whichButtonPressed = 0;
    }
}

bool RotatorClass::GetRainStatus()
{
    static int rainCount = 0;
    bool isRaining = false;
    if (!m_Config.rainCheckTwice)
        rainCount = 1;

    if (digitalRead(RAIN_SENSOR_PIN) == 1) {
        rainCount = 0;
    }
    else {
        if (digitalRead(RAIN_SENSOR_PIN) == 0) {
            if (rainCount == 1) {
                isRaining = true;
            }
            else {
                rainCount = 1;
            }
        }
    }
    return isRaining;
}

inline void RotatorClass::SetRainInterval(const unsigned long interval)
{
    m_Config.rainCheckInterval = interval;
    SaveToEEProm();
}

inline void RotatorClass::SetCheckRainTwice(const bool state)
{
    m_Config.rainCheckTwice = state;
    SaveToEEProm();
}


void RotatorClass::EnableMotor(const bool newState)
{
    if (!newState) {
        digitalWrite(STEPPER_ENABLE_PIN, M_DISABLE);
    }
    else {
        digitalWrite(STEPPER_ENABLE_PIN, M_ENABLE);
    }

}

long RotatorClass::GetMaxSpeed()
{
    return m_Config.maxSpeed;
}

void RotatorClass::SetMaxSpeed(const long newSpeed)
{
    m_Config.maxSpeed = newSpeed;
    stepper.setMaxSpeed(newSpeed);
    SaveToEEProm();
}

void RotatorClass::SetHomingCalibratingSpeed(const long newSpeed)
{
    stepper.setMaxSpeed(newSpeed);
}

void RotatorClass::RestoreNormalSpeed()
{
    stepper.setMaxSpeed(m_Config.maxSpeed);
}

long RotatorClass::GetAcceleration()
{
    return m_Config.acceleration;
}

void RotatorClass::SetAcceleration(const long newAccel)
{
    m_Config.acceleration = newAccel;
    stepper.setAcceleration(newAccel);
    SaveToEEProm();
}

bool RotatorClass::GetReversed()
{
    return m_Config.reversed;
}

void RotatorClass::SetReversed(const bool isReversed)
{
    m_Config.reversed = isReversed;
    stepper.setPinsInverted(isReversed, isReversed, isReversed);
    SaveToEEProm();
}

long RotatorClass::GetStepsPerRotation()
{
    return m_Config.stepsPerRotation;
}

inline byte RotatorClass::GetRainAction()
{
    return m_Config.rainAction;
}

inline unsigned long RotatorClass::GetRainCheckInterval()
{
    return m_Config.rainCheckInterval;
}

inline bool RotatorClass::GetRainCheckTwice()
{
    return m_Config.rainCheckTwice;
}

void RotatorClass::SetStepsPerRotation(const long newCount)
{
    m_fStepsPerDegree = (float)newCount / 360.0;
    m_Config.stepsPerRotation = newCount;
    SaveToEEProm();
}

void RotatorClass::StartHoming()
{
    float diff;
    long distance;

    if (m_bisAtHome) {
        SyncPosition(m_Config.homeAzimuth); // Set the Azimuth to the home position
        m_bHasBeenHomed = true; // We've been homed
        return;
    }

    // reduce speed by half
    SetHomingCalibratingSpeed(m_Config.maxSpeed/2);

    diff = GetAngularDistance(GetAzimuth(), GetHomeAzimuth());
    m_nMoveDirection = MOVE_POSITIVE;
    if (diff < 0)
        m_nMoveDirection = MOVE_NEGATIVE;

    distance = (m_Config.stepsPerRotation  * m_nMoveDirection * 1.5);
    m_seekMode = HOMING_HOME;
    MoveRelative(distance);
}

void RotatorClass::StartCalibrating()
{
    if (!m_bisAtHome)
        return;

    m_seekMode = CALIBRATION_MOVEOFF;
    // calibrate at half speed .. should increase precision
    SetHomingCalibratingSpeed(m_Config.maxSpeed/2);
    stepper.setCurrentPosition(0);
    m_moveOffUntilTimer.reset();
    m_bDoStepsPerRotation = false;
    MoveRelative(m_Config.stepsPerRotation  * 1.5);
}

void RotatorClass::Calibrate()
{
    if (m_seekMode > HOMING_HOME) {
        switch (m_seekMode) {
            case(CALIBRATION_MOVEOFF):
                if(m_moveOffUntilTimer.elapsed() >= m_nMoveOffUntilLapse) {
                    m_seekMode = CALIBRATION_MEASURE;
                }
                break;

            case(CALIBRATION_MEASURE):
                if (digitalRead(HOME_PIN) == 0) {
                    stepper.stop();
                    // restore speed
                    RestoreNormalSpeed();
                    m_seekMode = HOMING_NONE;
                    m_bHasBeenHomed = true;
                    m_bSetToHomeAzimuth = true;
                    m_bDoStepsPerRotation = true; // Once stopped, set SPR to stepper position and save to eeprom.
                }
                break;

            default:
                break;
        }
    }
}

void RotatorClass::SetHomeAzimuth(const float newHome)
{
    m_Config.homeAzimuth = newHome;
    SaveToEEProm();
}

inline void RotatorClass::SetRainAction(const byte value)
{
    m_Config.rainAction = value;
    SaveToEEProm();
}

float RotatorClass::GetHomeAzimuth()
{
    return m_Config.homeAzimuth;
}

void RotatorClass::SyncHome(const float newAzimuth)
{
    float delta;

    delta = GetAngularDistance(GetAzimuth(), newAzimuth);
    delta = delta + m_Config.homeAzimuth;
    if (delta < 0)
        delta = 360.0 + delta;

    if (delta >= 360.0)
        delta -= 360.0;

    m_Config.homeAzimuth = delta;
    SaveToEEProm();
}

int RotatorClass::GetHomeStatus()
{
    int status = NEVER_HOMED;

    if (m_bHasBeenHomed)
        status = HOMED;

    if (m_bisAtHome)
        status = ATHOME;
    return status;
}

void RotatorClass::SetParkAzimuth(const float newPark)
{
    m_Config.parkAzimuth = newPark;
    SaveToEEProm();
}

float RotatorClass::GetParkAzimuth()
{
    return m_Config.parkAzimuth;
}

int RotatorClass::GetSeekMode()
{
    return m_seekMode;
}

long RotatorClass::GetAzimuthToPosition(const float azimuth)
{
    long newPosition;

    newPosition = (float)m_Config.stepsPerRotation / (float)360 * azimuth;

    return newPosition;
}

void RotatorClass::SyncPosition(const float newAzimuth)
{
    long newPosition;

    newPosition = GetAzimuthToPosition(newAzimuth);
    stepper.setCurrentPosition(newPosition);
    //SaveToEEProm();
}

long RotatorClass::GetPosition()
{
    /// Return change in steps relative to
    /// last sync position
    long position;
    position = stepper.currentPosition();
    if (m_seekMode < CALIBRATION_MOVEOFF) {
        while (position >= m_Config.stepsPerRotation)
            position -= m_Config.stepsPerRotation;

        while (position < 0)
            position += m_Config.stepsPerRotation;
    }

    return position;
}

void RotatorClass::SetPosition(const long newPosition)
{
    /// Set movement tarGet by step position

    long currentPosition;

    EnableMotor(true);
    currentPosition = GetPosition();

    if (newPosition > currentPosition) {
        m_nMoveDirection = MOVE_POSITIVE;
    }
    else {
        m_nMoveDirection = MOVE_NEGATIVE;
    }
    EnableMotor(true);
    stepper.moveTo(newPosition);
}

void RotatorClass::MoveRelative(const long howFar)
{
    // Use by Home and Calibrate
    // Tells dome to rotate more than 360 degrees
    // from current position. Stopped only by
    // homing or calibrating routine.
    EnableMotor(true);
    m_nMoveDirection = -1;  // MOVE_NEGATIVE ?
    if (howFar > 0)
        m_nMoveDirection = 1; // MOVE_POSITIVE ?
    else if(howFar == 0 )
        m_nMoveDirection = 0;

    stepper.move(howFar);
}

int RotatorClass::GetDirection()
{
    return m_nMoveDirection;
}

float RotatorClass::GetAngularDistance(const float fromAngle, const float toAngle)
{
    float delta;
    delta = toAngle - fromAngle;
    if (delta == 0)
        return 0; //  we are already there

    if (delta > 180)
        delta -= 360;

    if (delta < -180)
        delta += 360;

    return delta;
}


void RotatorClass::GoToAzimuth(const float newHeading)
{
    // Goto new target
    float currentHeading;
    float delta;

    currentHeading = GetAzimuth();
    delta = GetAngularDistance(currentHeading, newHeading) * m_fStepsPerDegree;
    delta = delta - int(delta) % STEP_TYPE;
    if(delta == 0) {
        m_nMoveDirection = MOVE_NONE;
        return;
    }

    MoveRelative(delta);
}

float RotatorClass::GetAzimuth()
{
    /* floating point math for Get azimuth
    *  prefer floats for accurately placing
    *  the power pickup to charge the shutter
    *  but it introduces significant processing
    *  overhead
    */
    float azimuth = 0;
    long currentPosition = 0;

    currentPosition = GetPosition();
    if (currentPosition != 0)
        azimuth = (float)GetPosition() / (float)m_Config.stepsPerRotation * 360.0;
    //  in case we need to do another step
    //  do a run sequence now
    while (azimuth < 0)
        azimuth += 360.0;

    while (azimuth >= 360.0)
        azimuth -= 360.0;

    return azimuth;
}

#ifndef STANDALONE
String RotatorClass::GetPANID()
{
    return String(m_Config.panid, HEX);
}


void RotatorClass::setPANID(const String panID)
{
    m_Config.panid = strtol(panID.c_str(), 0, 16);
    SaveToEEProm();
}

bool RotatorClass::isRadioConfigured()
{
    return m_Config.radioIsConfigured;
}

void RotatorClass::setRadioConfigured(bool bConfigured)
{
    m_Config.radioIsConfigured = bConfigured;
}
#endif


void RotatorClass::Run()
{
    static bool wasRunning = false;
    long stepsFromZero;


    if (m_buttonCheckTimer.elapsed() >= m_nNextCheckButtonLapse) {
        ButtonCheck();
        m_buttonCheckTimer.reset();
    }

    if (m_periodicReadingTimer.elapsed() >= m_nNextPeriodicReadingLapse) {
        m_nVolts = ReadVolts();
        m_periodicReadingTimer.reset();
    }

    m_bisAtHome = false; // default to not at home switch

    if (m_seekMode > HOMING_HOME)
        Calibrate();

    if (stepper.isRunning()) {

        wasRunning = true;
        if (m_seekMode == HOMING_HOME && digitalRead(HOME_PIN) == 0) { // We're looking for home and found it
            Stop();
            // restore max speed
            RestoreNormalSpeed();
            m_bSetToHomeAzimuth = true; // Need to set current az to homeaz but not until rotator is stopped;
            m_seekMode = HOMING_NONE;
            m_bHasBeenHomed = true;
            return;
        }
    }

    if (stepper.isRunning())
        return;

    // Won't get here if stepper is moving
    if (digitalRead(HOME_PIN) == 0 ) { // Not moving and we're at home
        m_bisAtHome = true;
        if (!m_bHasBeenHomed) { // Just started up rotator so tell rotator its at home.
            SyncPosition(m_Config.homeAzimuth); // Set the Azimuth to the home position
            m_bHasBeenHomed = true; // We've been homed
        }
    }

    if (wasRunning)
    {
        m_nMoveDirection = MOVE_NONE;

        if (m_bDoStepsPerRotation) {
            m_Config.stepsPerRotation = stepper.currentPosition();
            SyncHome(m_Config.homeAzimuth);
            SaveToEEProm();
            m_bDoStepsPerRotation = false;
        }

        stepsFromZero = GetPosition();
        if (stepsFromZero < 0) {
            while (stepsFromZero < 0)
                stepsFromZero += m_Config.stepsPerRotation;

            stepper.setCurrentPosition(stepsFromZero);
        }

        if (stepsFromZero > m_Config.stepsPerRotation) {
            while (stepsFromZero > m_Config.stepsPerRotation)
                stepsFromZero -= m_Config.stepsPerRotation;

            stepper.setCurrentPosition(stepsFromZero);
        }

        if (m_bSetToHomeAzimuth) {
            SyncPosition(m_Config.homeAzimuth);
            m_bSetToHomeAzimuth = false;
        }

        EnableMotor(false);
        wasRunning = false;
    } // end if (wasRunning)
}

void RotatorClass::Stop()
{
    // It takes approximately RunSpeed/3.95 steps to stop
    // Use this to calculate a full step stopping position
    // Actual divisor appears to be 3.997 but this leaves a
    // few extra steps for getting to a full step position.

    if (!stepper.isRunning())
        return;

    RestoreNormalSpeed();
    m_seekMode = HOMING_NONE;
    stepper.stop();
}
