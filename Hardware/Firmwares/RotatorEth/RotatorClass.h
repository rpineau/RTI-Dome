//
// RTI-Zone Dome Rotator firmware. Based on https://github.com/nexdome/Automation/tree/master/Firmwares
// As I contributed to the "old" 2,x firmware and was somewhat familiar with it I decided to reuse it and
// fix most of the known issues. I also added some feature related to XBee init and reset.
// This is meant to run on an Arduino DUE as we put the AccelStepper run() call in an interrupt
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

// set this to match the type of steps configured on the
// stepper controller
#define STEP_TYPE 8

#define DEBUG   // enable debug to DebugPort serial port
#ifdef DEBUG
#define DBPrint(x) if(DebugPort) DebugPort.print(x)
#define DBPrintln(x) if(DebugPort) DebugPort.println(x)
#define DBPrintHex(x) if(DebugPort) DebugPort.print(x, HEX)
#else
#define DBPrint(x)
#define DBPrintln(x)
#define DBPrintHex(x)
#endif // DEBUG

// Arduino boards
#define HOME_PIN             2  // Also used for Shutter open status
#define BUTTON_CCW           5  // Digital Input
#define BUTTON_CW            6  // Digital Input
#define RAIN_SENSOR_PIN      7  // Digital Input from RG11
#define STEPPER_ENABLE_PIN  10  // Digital Output
#define DIRECTION_PIN       11  // Digital Output
#define STEP_PIN            12  // Digital Output
#define BUFFERN_EN          57  // Digital output to enable 74LVC245 buffer

#define VOLTAGE_MONITOR_PIN A0
#define AD_REF      3.3
#define RES_MULT    5.0 // resistor voltage divider on the shield


#define MOVE_NEGATIVE       -1
#define MOVE_NONE            0
#define MOVE_POSITIVE        1

#define M_ENABLE    HIGH
#define M_DISABLE   LOW

#define MAX_SPEED           8000
#define ACCELERATION        7000

/*
Micro-steps per rotation with original motor and 15.3:1 gearbox
    NexDome 2m      : 440640
    Explora-Dome 8' : 479800
*/

#define STEPS_DEFAULT       440640

// DM556T stepper controller min pulse width  = 2.5uS
// #define MIN_PULSE_WIDTH 3

// ISD02/04/08 stepper controller min pulse width = 5uS at 1600rev/s (8 microsteps).
// TB6600 tepper controller min pulse width = 5uS
#define MIN_PULSE_WIDTH 5


// used to offset the config location.. at some point.
#define EEPROM_LOCATION     0  // not used with Arduino Due flash
#define EEPROM_SIGNATURE    2645

#ifdef USE_ETHERNET
typedef struct IPCONFIG {
    bool            bUseDHCP;
    IPAddress       ip;
    IPAddress       dns;
    IPAddress       gateway;
    IPAddress       subnet;
} IPConfig;
#endif // USE_ETHERNET

typedef struct RotatorConfiguration {
    int             signature;
    long            stepsPerRotation;
    long            acceleration;
    long            maxSpeed;
    bool            reversed;
    float           homeAzimuth;
    float           parkAzimuth;
    int             cutOffVolts;
    int             rainAction;
#ifndef STANDALONE
    int             panid;
#endif // STANDALONE
#ifdef USE_ETHERNET
    IPConfig        ipConfig;
#endif // USE_ETHERNET
} Configuration;


enum HomeStatuses { NOT_AT_HOME, HOMED, ATHOME };
enum Seeks { HOMING_NONE,           // Not homing or calibrating
            HOMING_HOME,            // Homing
            HOMING_FINISH,          // found home
            HOMING_BACK_HOME,       //backing out to home Az
            CALIBRATION_MOVE_OFF,    // Ignore home until we've moved off while measuring the dome.
            CALIBRATION_STEP1,      // this is the mode until we hit the home sensor on the first pass
            CALIBRATION_MOVE_OFF2,   // we need to clear the home sensor again
            CALIBRATION_MEASURE     // Measuring dome until home hit again.
};

enum RainActions {DO_NOTHING=0, HOME, PARK};


AccelStepper stepper(AccelStepper::DRIVER, STEP_PIN, DIRECTION_PIN);

// Arduino interrupt timer
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


class RotatorClass
{

public:

    RotatorClass();

    void		SaveToEEProm();

    // rain sensor methods
    bool		GetRainStatus();
    int			GetRainAction();
    void		SetRainAction(const int);
    void		rainInterrupt();

    // motor methods
    long        GetAcceleration();
    void        SetAcceleration(const long);

    long        GetMaxSpeed();
    void        SetMaxSpeed(const long);

    long        GetPosition();
    float       GetAzimuth();
    long        GetAzimuthToPosition(const float);
    void        SyncPosition(const float);
    void        GoToAzimuth(const float);

    bool        GetReversed();
    void        SetReversed(const bool reversed);
    int         GetDirection();

    long        GetStepsPerRotation();
    void        SetStepsPerRotation(const long);

    int         restoreDefaultMotorSettings();

    float       GetAngularDistance(const float fromAngle, const float toAngle);

    // Voltage methods
    int         GetLowVoltageCutoff();
    void        SetLowVoltageCutoff(const int);
    bool        GetVoltsAreLow();
    String      GetVoltString();


    // home and park methods
    float       GetHomeAzimuth();
    void        SetHomeAzimuth(const float);
    int         GetHomeStatus();

    float       GetParkAzimuth();
    void        SetParkAzimuth(const float);

    int         GetSeekMode();

#ifndef STANDALONE
    // Xbee
    String      GetPANID();
    void        setPANID(const String panID);
#endif // STANDALONE

    // Homing and Calibration
    void        StartHoming();
    void        StartCalibrating();
    void        Calibrate();

    // Movers
    void        EnableMotor(const bool);
    void        MoveRelative(const long steps);
    void        Run();
    void        Stop();
    void        motorStop();
    void        motorMoveTo(const long newPosition);
    void        motorMoveRelative(const long howFar);
    void        stopInterrupt();
    void        homeInterrupt();

    void		ButtonCheck();

    void        bufferEnable(bool bEnable);

#ifdef USE_ETHERNET
    void        getIpConfig(IPConfig &config);
    bool        getDHCPFlag();
    void        setDHCPFlag(bool bUseDHCP);
    String      getIPAddress();
    void        setIPAddress(String ipAddress);
    String      getIPSubnet();
    void        setIPSubnet(String ipSubnet);
    String      getIPGateway();
    void        setIPGateway(String ipGateway);
    String      IpAddress2String(const IPAddress& ipAddress);
#endif // USE_ETHERNET

private:
    Configuration   m_Config;

    // Rotator
    bool            m_bWasRunning;
    bool            m_bisAtHome;
    volatile enum Seeks	m_seekMode;
    bool            m_bSetToHomeAzimuth;
    bool            m_bDoStepsPerRotation;

    float           m_fStepsPerDegree;
    StopWatch       m_MoveOffUntilTimer;
    unsigned long   m_nMOVE_OFFUntilLapse = 2000;
    int             m_nMoveDirection;

	volatile long	m_nStepsAtHome;
    volatile long	m_nHomePosEdgePass1;
    volatile long	m_nHomePosEdgePass2;
    volatile bool	m_HomeFound;

    // Power values
    float           m_fAdcConvert;
    int             m_nVolts;
    int             ReadVolts();


    StopWatch       m_periodicReadingTimer;
    unsigned long   m_nNextPeriodicReadingLapse = 10;


    // Utility
    bool        LoadFromEEProm();
    void        SetDefaultConfig();

    volatile bool        m_bIsRaining;

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



RotatorClass::RotatorClass()
{
#ifdef USE_EXT_EEPROM
    DBPrintln("Using external AT24AA128 eeprom");
    Wire1.begin();
    // AT24AA128 page size is 64 byte
    m_EEPROMpageSize = 64;
#endif

    m_seekMode = HOMING_NONE;
    m_bWasRunning = false;
    m_bisAtHome = false;
    m_HomeFound = false;
    m_bSetToHomeAzimuth = false;
    m_bDoStepsPerRotation = false;
    m_nMoveDirection = MOVE_NONE;

    // input

    pinMode(HOME_PIN,               INPUT);
    pinMode(BUTTON_CCW,             INPUT);
    pinMode(BUTTON_CW,              INPUT);
    pinMode(RAIN_SENSOR_PIN,        INPUT);
    pinMode(VOLTAGE_MONITOR_PIN,    INPUT);

    // output
    pinMode(STEP_PIN,               OUTPUT);
    pinMode(DIRECTION_PIN,          OUTPUT);
    pinMode(STEPPER_ENABLE_PIN,     OUTPUT);
    pinMode(BUFFERN_EN,             OUTPUT);

    LoadFromEEProm();

    m_bDoEEPromSave = false;  // we just read the config, no need to resave all the value we're setting
    SetMaxSpeed(m_Config.maxSpeed);
    SetAcceleration(m_Config.acceleration);
    SetStepsPerRotation(m_Config.stepsPerRotation);
    SetReversed(m_Config.reversed);
    // set pulse width
    stepper.setMinPulseWidth(MIN_PULSE_WIDTH); // 5uS to test. Default in the source seems to be set to 1 ...

#ifndef STANDALONE
    if(m_Config.panid <= 0) { // set to default.. there was something bad in eeprom.
        m_Config.panid = 0x4242;
    }
#endif // STANDALONE
    m_bDoEEPromSave = true;


    // enable buffers to read raind and home sensor, only needed on old protype board
    bufferEnable(true);

    if (digitalRead(RAIN_SENSOR_PIN) == LOW) {
        m_bIsRaining = true;
    }
    else {
        m_bIsRaining = false;
    }

    if(digitalRead(HOME_PIN) == LOW) {
        // we're at the home position
        m_bisAtHome = true;
        SyncPosition(m_Config.homeAzimuth);
        DBPrintln("At home on startup");
    }
    else {
        //if not at home on power up, assume we're at the park position
        SyncPosition(m_Config.parkAzimuth);
        DBPrintln("At park on startup");
    }

    m_fAdcConvert = RES_MULT * (AD_REF / 1023.0) * 100;


    // reset all timers
    m_MoveOffUntilTimer.reset();
    m_periodicReadingTimer.reset();
}


inline void RotatorClass::homeInterrupt()
{
    long  nPos;

	// debounce
	if (digitalRead(HOME_PIN) != LOW)
		return;

    nPos = stepper.currentPosition(); // read position immediately

    switch(m_seekMode) {
        case HOMING_HOME: // stop and take note of where we are so we can reverse.
            m_nStepsAtHome = nPos;
            motorStop();
            m_HomeFound = true;
            break;

        case CALIBRATION_STEP1: // take note of the first edge
            m_nHomePosEdgePass1 = nPos;
            m_seekMode = CALIBRATION_MOVE_OFF2; // let's not be fooled by the double trigger
            m_MoveOffUntilTimer.reset();
            break;

        case CALIBRATION_MEASURE: // stop and take note of where we are so we can reverse.
            m_nStepsAtHome = nPos;
            m_nHomePosEdgePass2 = m_nStepsAtHome;
            motorStop();
            break;

        default: // resync
            // SyncPosition(m_Config.homeAzimuth); // THIS STOPS THE MOTOR :( Thanks AccelStepper :((
            break;
    }
}


inline void RotatorClass::rainInterrupt()
{
    if (digitalRead(RAIN_SENSOR_PIN) == LOW) {
        m_bIsRaining = true;
    }
    else
        m_bIsRaining = false;
}

void RotatorClass::SaveToEEProm()
{
    if(!m_bDoEEPromSave)
        return;

    DBPrintln("RotatorClass::SaveToEEProm");

    m_Config.signature = EEPROM_SIGNATURE;

#ifdef USE_EXT_EEPROM
    writeEEPROM(EEPROM_ADDR, EEPROM_LOCATION, (byte *) &m_Config, sizeof(Configuration));
#else
    byte data[sizeof(Configuration)];
    memcpy(data, &m_Config, sizeof(Configuration));
    dueFlashStorage.write(0, data, sizeof(Configuration));
#endif
}

bool RotatorClass::LoadFromEEProm()
{
    bool response = true;

    DBPrintln("RotatorClass::LoadFromEEProm");
    //  zero the structure so currently unused parts
    //  dont end up loaded with random garbage
    memset(&m_Config, 0, sizeof(Configuration));
#ifdef USE_EXT_EEPROM
    readEEPROMBuffer(EEPROM_ADDR, EEPROM_LOCATION, (byte *) &m_Config, sizeof(Configuration) );

#else
    byte* data = dueFlashStorage.readAddress(0);
    memcpy(&m_Config, data, sizeof(Configuration));
#endif

    if (m_Config.signature != EEPROM_SIGNATURE) {
        SetDefaultConfig();
        SaveToEEProm();
        response = false;
    }
    return response;
}

void RotatorClass::SetDefaultConfig()
{
    memset(&m_Config, 0, sizeof(Configuration));

    m_Config.signature = EEPROM_SIGNATURE;
    m_Config.maxSpeed = MAX_SPEED;
    m_Config.acceleration = ACCELERATION;
    m_Config.stepsPerRotation = STEPS_DEFAULT;
    m_Config.reversed = 0;
    m_Config.homeAzimuth = 0;
    m_Config.parkAzimuth = 0;
    m_Config.cutOffVolts = 1150;
    m_Config.rainAction = DO_NOTHING;
#ifndef STANDALONE
    m_Config.panid = 0x4242;
#endif // STANDALONE
#ifdef USE_ETHERNET
    m_Config.ipConfig.bUseDHCP = true;
    m_Config.ipConfig.ip.fromString("192.168.0.99");
    m_Config.ipConfig.dns.fromString("192.168.0.1");
    m_Config.ipConfig.gateway.fromString("192.168.0.1");
    m_Config.ipConfig.subnet.fromString("255.255.255.0");
#endif // USE_ETHERNET
}

#ifdef USE_ETHERNET
void RotatorClass::getIpConfig(IPConfig &config)
{
    config.bUseDHCP = m_Config.ipConfig.bUseDHCP;
    config.ip = m_Config.ipConfig.ip;
    config.dns = m_Config.ipConfig.dns;
    config.gateway = m_Config.ipConfig.gateway;
    config.subnet = m_Config.ipConfig.subnet;
}


bool RotatorClass::getDHCPFlag()
{
    return m_Config.ipConfig.bUseDHCP;
}

void RotatorClass::setDHCPFlag(bool bUseDHCP)
{
    m_Config.ipConfig.bUseDHCP = bUseDHCP;
    DBPrintln("New bUseDHCP : " + bUseDHCP?"Yes":"No");
    SaveToEEProm();
}

String RotatorClass::getIPAddress()
{
    return IpAddress2String(m_Config.ipConfig.ip);
}

void RotatorClass::setIPAddress(String ipAddress)
{
    m_Config.ipConfig.ip.fromString(ipAddress);
    DBPrintln("New IP address : " + IpAddress2String(m_Config.ipConfig.ip));
    SaveToEEProm();
}

String RotatorClass::getIPSubnet()
{
    return IpAddress2String(m_Config.ipConfig.subnet);
}

void RotatorClass::setIPSubnet(String ipSubnet)
{
    m_Config.ipConfig.subnet.fromString(ipSubnet);
    DBPrintln("New subnet mask : " + IpAddress2String(m_Config.ipConfig.subnet));
    SaveToEEProm();
}

String RotatorClass::getIPGateway()
{
    return IpAddress2String(m_Config.ipConfig.gateway);
}

void RotatorClass::setIPGateway(String ipGateway)
{
    m_Config.ipConfig.gateway.fromString(ipGateway);
    DBPrintln("New gateway : " + IpAddress2String(m_Config.ipConfig.gateway));

    // setting DNS IP to gateway IP as we don't use it and this is probably correct for most home users
    m_Config.ipConfig.dns.fromString(ipGateway);
    SaveToEEProm();
}

String RotatorClass::IpAddress2String(const IPAddress& ipAddress)
{
    return String() + ipAddress[0] + "." + ipAddress[1] + "." + ipAddress[2] + "." + ipAddress[3];;
}
#endif // USE_ETHERNET

//
// rain sensor methods
//
bool RotatorClass::GetRainStatus()
{
    if (digitalRead(RAIN_SENSOR_PIN) == LOW) {
        m_bIsRaining = true;
    }
    else
        m_bIsRaining = false;

    return m_bIsRaining;
}

inline int RotatorClass::GetRainAction()
{
    return m_Config.rainAction;
}

inline void RotatorClass::SetRainAction(const int value)
{
    m_Config.rainAction = value;
    SaveToEEProm();
}

//
// motor methods
//
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

long RotatorClass::GetPosition()
{
    /// Return change in steps relative to
    /// last sync position
    long position;
    position = stepper.currentPosition();
    if (m_seekMode < CALIBRATION_MOVE_OFF) {
        while (position >= m_Config.stepsPerRotation)
            position -= m_Config.stepsPerRotation;

        while (position < 0)
            position += m_Config.stepsPerRotation;
    }

    return position;
}


float RotatorClass::GetAzimuth()
{
    double azimuth = 0.0;
    long currentPosition = 0;

    currentPosition = GetPosition();
    if (currentPosition != 0)
        azimuth = (double)currentPosition / (double)m_Config.stepsPerRotation * 360.0;

    while (azimuth < 0.0)
        azimuth += 360.0;

    while (azimuth >= 360.0) {
        azimuth -= 360.0;
    }

    return float(azimuth);
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

int RotatorClass::GetDirection()
{
    return m_nMoveDirection;
}

long RotatorClass::GetStepsPerRotation()
{
    return m_Config.stepsPerRotation;
}

void RotatorClass::SetStepsPerRotation(const long newCount)
{
    m_fStepsPerDegree = (float)newCount / 360.0;
    m_Config.stepsPerRotation = newCount;
    SaveToEEProm();
}

int RotatorClass::restoreDefaultMotorSettings()
{
    m_Config.maxSpeed = MAX_SPEED;
    m_Config.acceleration = ACCELERATION;
    m_Config.stepsPerRotation = STEPS_DEFAULT;
    SetMaxSpeed(m_Config.maxSpeed);
    SetAcceleration(m_Config.acceleration);
    SetStepsPerRotation(m_Config.stepsPerRotation);
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

//
// Voltage methods
//
int RotatorClass::GetLowVoltageCutoff()
{
    return m_Config.cutOffVolts;
}

void RotatorClass::SetLowVoltageCutoff(const int lowVolts)
{
    m_Config.cutOffVolts = lowVolts;
    SaveToEEProm();
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

int RotatorClass::ReadVolts()
{
    int adc;
    float calc;

    adc = analogRead(VOLTAGE_MONITOR_PIN);
    calc = adc * m_fAdcConvert;
    return int(calc);
}

//
// home and park methods
//
float RotatorClass::GetHomeAzimuth()
{
    return m_Config.homeAzimuth;
}

void RotatorClass::SetHomeAzimuth(const float newHome)
{
    m_Config.homeAzimuth = newHome;
    SaveToEEProm();
}

int RotatorClass::GetHomeStatus()
{
    int status = NOT_AT_HOME;

    if(digitalRead(HOME_PIN) == LOW)
        m_bisAtHome = true;
	else
		m_bisAtHome = false;

    if (m_bisAtHome)
        status = ATHOME;
    return status;
}

float RotatorClass::GetParkAzimuth()
{
    return m_Config.parkAzimuth;
}

void RotatorClass::SetParkAzimuth(const float newPark)
{
    m_Config.parkAzimuth = newPark;
    SaveToEEProm();
}

int RotatorClass::GetSeekMode()
{
    return m_seekMode;
}

//
// Xbee
//
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

#endif // STANDALONE


//
// Homing and Calibration
//
void RotatorClass::StartHoming()
{
    float diff;
    long distance;

    if(digitalRead(HOME_PIN) == LOW) {
        // we're at the home position
        m_bisAtHome = true;
        SyncPosition(m_Config.homeAzimuth);
        DBPrintln("At home on startup");
    }
	m_bisAtHome = false;
    m_HomeFound = false;
    // Always home in the same direction as we don't
    // know the width of the home magnet in steps.
    // We use edge interrupt to detect the left edge of the magnet as home.
    m_nMoveDirection = MOVE_POSITIVE;
    distance = (160000000L  * m_nMoveDirection);
    m_seekMode = HOMING_HOME;
    MoveRelative(distance);
}

void RotatorClass::StartCalibrating()
{
    stepper.setCurrentPosition(0);
    m_bDoStepsPerRotation = false;
    m_nHomePosEdgePass1 = 0;
    m_nHomePosEdgePass2 = 0;

    if(m_bisAtHome) {
        m_MoveOffUntilTimer.reset();
        m_seekMode = CALIBRATION_MOVE_OFF;
        MoveRelative(-5000);
    }
    else {
        m_seekMode = CALIBRATION_STEP1;
        MoveRelative(160000000L);
    }
}

void RotatorClass::Calibrate()
{
    if (m_seekMode > HOMING_HOME) {
        switch (m_seekMode) {
            case(CALIBRATION_MOVE_OFF):
                if (!stepper.isRunning()) {
                    m_seekMode = CALIBRATION_STEP1;
                    stepper.setCurrentPosition(0);
                    MoveRelative(160000000L);
                }
                break;

            case(CALIBRATION_MOVE_OFF2):
                if(m_MoveOffUntilTimer.elapsed() >= m_nMOVE_OFFUntilLapse) {
                    m_seekMode = CALIBRATION_MEASURE;
                }
                break;

            case(CALIBRATION_MEASURE):
                if (!stepper.isRunning()) { // we have to wait for it to have stopped
                    m_seekMode = HOMING_FINISH;
                    m_bSetToHomeAzimuth = true;
                    m_bDoStepsPerRotation = true; // Once stopped, set SPR to stepper position and save to eeprom.
                }
                break;

            default:
                break;
        }
    }
}

//
// Movers
//
void RotatorClass::EnableMotor(const bool bEnabled)
{
    if (!bEnabled) {
        digitalWrite(STEPPER_ENABLE_PIN, M_DISABLE);
        stopInterrupt();
    }
    else {
        digitalWrite(STEPPER_ENABLE_PIN, M_ENABLE);
    }

}

void RotatorClass::MoveRelative(const long howFar)
{
    // Use by Home and Calibrate
    // Tells dome to rotate more than 360 degrees
    // from current position. Stopped only by
    // homing or calibrating routine.
    EnableMotor(true);
    m_nMoveDirection = MOVE_NEGATIVE;
    if (howFar > 0)
        m_nMoveDirection = MOVE_POSITIVE;
    else if(howFar == 0 )
        m_nMoveDirection = MOVE_NONE;
    m_bisAtHome = false;
    motorMoveRelative(howFar);
}


void RotatorClass::ButtonCheck()
{
    if (digitalRead(BUTTON_CW) == LOW) {
        MoveRelative(160000000L);
    }
    else if (digitalRead(BUTTON_CCW) == LOW)  {
        MoveRelative(-160000000L);
    }
    else {
        Stop();
    }
}

void RotatorClass::bufferEnable(bool bEnable)
{
    if(bEnable)
        digitalWrite(BUFFERN_EN, 0);
    else
        digitalWrite(BUFFERN_EN, 1);
}

void RotatorClass::Run()
{
    long stepsFromZero;
    long position;
    float azimuthDelta;

    if (m_periodicReadingTimer.elapsed() >= m_nNextPeriodicReadingLapse) {
        m_nVolts = ReadVolts();
        m_periodicReadingTimer.reset();
    }

    if (m_seekMode > HOMING_HOME)
        Calibrate();

    if (stepper.isRunning()) {
        m_bWasRunning = true;
        if (m_seekMode == HOMING_HOME && m_HomeFound) { // We're looking for home and found it
            Stop();
            m_bSetToHomeAzimuth = true; // Need to set home az but not until rotator is stopped;
            m_seekMode = HOMING_FINISH;
            return;
        }
    }

    if (stepper.isRunning())
        return;

    if( m_seekMode == HOMING_BACK_HOME) {
        m_bisAtHome = true; // we're back home and done homing.
        m_seekMode = HOMING_NONE;
    }

    if (m_bDoStepsPerRotation) {
        m_bDoStepsPerRotation = false;
        SetStepsPerRotation(m_nHomePosEdgePass2 - m_nHomePosEdgePass1);
        SaveToEEProm();
        position = stepper.currentPosition();
        azimuthDelta = (float)(position - m_nHomePosEdgePass2) / m_fStepsPerDegree;
        SyncPosition(azimuthDelta + m_Config.homeAzimuth);
        m_nStepsAtHome = 0;
    }

    if (m_bSetToHomeAzimuth) {
        m_bSetToHomeAzimuth = false;
        position = stepper.currentPosition();
        azimuthDelta = (float)(position - m_nStepsAtHome) / m_fStepsPerDegree;
        SyncPosition(azimuthDelta + m_Config.homeAzimuth);
        position = stepper.currentPosition();
        GoToAzimuth(m_Config.homeAzimuth); // moving to home now that we know where we are
        m_seekMode = HOMING_BACK_HOME;
    }

    if (m_bWasRunning) {
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

        if( m_seekMode == HOMING_NONE) {
            // not moving anymore ..
            m_nMoveDirection = MOVE_NONE;
            EnableMotor(false);
            m_bWasRunning = false;
            // check if we stopped on the home sensor
            if(digitalRead(HOME_PIN) == LOW) {
                // we're at the home position
                m_bisAtHome = true;
            }
        }
    } // end if (m_bWasRunning)
}

void RotatorClass::Stop()
{
    // It takes approximately RunSpeed/3.95 steps to stop
    // Use this to calculate a full step stopping position
    // Actual divisor appears to be 3.997 but this leaves a
    // few extra steps for getting to a full step position.
    DBPrintln("RotatorClass::Stop");
    if (!stepper.isRunning())
        return;

    m_seekMode = HOMING_NONE;
    motorStop();
}



void RotatorClass::motorStop()
{
    DBPrintln("RotatorClass::motorStop");
    stepper.stop();
}

void RotatorClass::stopInterrupt()
{
    DBPrintln("Stopping motor interrupt");
    // stop interrupt timer
    stopTimer(TC1, 0, TC3_IRQn);
}

void RotatorClass::motorMoveTo(const long newPosition)
{

    stepper.moveTo(newPosition);
    DBPrintln("Starting motor interrupt");
    int nFreq;
    nFreq = m_Config.maxSpeed *3 >20000 ? 20000 : m_Config.maxSpeed*3;
    // start interrupt timer
    // AccelStepper run() is called under a timer interrupt
    startTimer(TC1, 0, TC3_IRQn, nFreq);
}

void RotatorClass::motorMoveRelative(const long howFar)
{

    stepper.move(howFar);
    DBPrintln("Starting motor interrupt");
    int nFreq;
    nFreq = m_Config.maxSpeed *3 >20000 ? 20000 : m_Config.maxSpeed*3;
    // start interrupt timer
    // AccelStepper run() is called under a timer interrupt
    startTimer(TC1, 0, TC3_IRQn, nFreq);
}

#ifdef USE_EXT_EEPROM

//
// EEProm code to access the AT24AA128 I2C eeprom
//

// read one byte
byte RotatorClass::readEEPROMByte(int deviceaddress, unsigned int eeaddress)
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
void RotatorClass::readEEPROMBuffer(int deviceaddress, unsigned int eeaddress, byte *buffer, int length)
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
void RotatorClass::readEEPROMBlock(int deviceaddress, unsigned int eeaddress, byte *data, int offset, int length)
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
void RotatorClass::writeEEPROM(int deviceaddress, unsigned int eeaddress, byte *data, int length)
{
	int c = length;					// bytes left to write
	int offD = 0;					// current offset in data pointer
	int offP;						// current offset in page
	int nc = 0;						// next n bytes to write

	// write all bytes in multiple steps
	while (c > 0) {
		// calc offset in page
		offP = eeaddress % m_EEPROMpageSize;
		// maximal 30 bytes to write
		nc = min(min(c, I2C_CHUNK_SIZE), m_EEPROMpageSize - offP);
		writeEEPROMBlock(deviceaddress, eeaddress, data, offD, nc);
		c-=nc;
		offD+=nc;
		eeaddress+=nc;
	}
}

// Write a buffer to EEPROM
void RotatorClass::writeEEPROMBlock(int deviceaddress, unsigned int eeaddress, byte *data, int offset, int length)
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



