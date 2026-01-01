//
// RTI-Zone Dome Rotator firmware.
//
//  Copyright © 2024 Rodolphe Pineau. All rights reserved.
//
//

#include <Preferences.h>
#include <FastAccelStepper.h>
#include <nvs_flash.h>
#include "StopWatch.h"
#include "config.h"

volatile bool bIntterruptHappened = false;
volatile int intType = 0;

typedef struct IPCONFIG {
	bool            bUseDHCP;
	IPAddress       ip;
	IPAddress       dns;
	IPAddress       gateway;
	IPAddress       subnetMask;
} IPConfig;

#ifdef USE_WIFI
typedef struct WIFICONFIG {
	IPAddress       ip;
	String 			sSSID;
	String			sPassword;
} WIFIConfig;
#endif // USE_WIFI

typedef struct RotatorConfiguration {
	long            stepsPerRotation;
	long            acceleration;
	long            maxSpeed;
	bool            reversed;
	float           homeAzimuth;
	float           parkAzimuth;
	int             conditionsAction;
	IPConfig        ipConfig;
#ifdef USE_WIFI
	// Use WiFi instead of XBee
	WIFIConfig		wifiIpConfig;
#endif
} Configuration;


enum HomeStatuses { NOT_AT_HOME, HOMED, ATHOME };
enum Seeks { NOT_MOVING,           // Not homing or calibrating
			MOVING_GOTO,
			HOMING_HOME,            // Homing
			HOMING_FINISH,          // found home
			HOMING_BACK_HOME,       //backing out to home Az
			CALIBRATION_MOVE_OFF,    // Ignore home until we've moved off while measuring the dome.
			CALIBRATION_STEP1,      // this is the mode until we hit the home sensor on the first pass
			CALIBRATION_MOVE_OFF2,   // we need to clear the home sensor again
			CALIBRATION_MEASURE     // Measuring dome until home hit again.
};

enum ConditionsActions {DO_NOTHING=0, HOME, PARK};
enum ConditionSensorStates {UNSAFE= 0, COND_SAFE, COND_UNKNOWN};

FastAccelStepperEngine engine = FastAccelStepperEngine();
FastAccelStepper *stepper = NULL;

class RotatorClass
{

public:

	RotatorClass();

	// condition sensor methods
	bool		GetConditionsStatus();
	int			GetConditionsAction();
	void		SetConditionsAction(const int);
	void		conditionsInterrupt();

	// motor methods
	long        GetAcceleration();
	void        SetAcceleration(const long);

	long        GetMaxSpeed();
	void        SetMaxSpeed(const long);

	long        GetPosition();
	float		GetAzimuth();
	long        GetAzimuthToPosition(const float);
	void        SyncPosition(const float);
	void        GoToAzimuth(const float);

	bool        GetReversed();
	void        SetReversed(const bool reversed);
	int         GetDirection();

	long        GetStepsPerRotation();
	void        SetStepsPerRotation(const long);

	void        restoreDefaultMotorSettings();

	float		GetAngularDistance(const float fromAngle, const float toAngle);

	// home and park methods
	float		GetHomeAzimuth();
	void        SetHomeAzimuth(const float);
	int         GetHomeStatus();

	float		GetParkAzimuth();
	void        SetParkAzimuth(const float);

	int         GetSeekMode();

	// Homing and Calibration
	void        StartHoming();
	void        StartCalibrating();
	void        Calibrate();

	// Movers
	void        MoveRelative(const long steps);
	void        Run();
	void        Stop();
	void        motorStop();
	void        motorMoveRelative(const long howFar);
	void        homeInterrupt();

	void		ButtonCheck();
	bool 		checkBoundaries(float dTargetAz, float dDomeAz, float dMargin);

	void        getIpConfig(IPConfig &config);
	bool        getDHCPFlag();
	void        setDHCPFlag(bool bUseDHCP);
	String      getIPAddress();
	void        setIPAddress(String ipAddress);
	String      getIPSubnetMask();
	void        setIPSubnetMask(String subnetMask);
	String      getIPGateway();
	void        setIPGateway(String ipGateway);

#ifdef USE_WIFI
	String      getSSID();
	void        setSSID(String sSSID);
	void		getWiFiConfig(WIFIConfig &config);
	void		setWifiDefault();
#endif // USE_WIFI

	static String IpAddress2String(const IPAddress& ipAddress);
private:
	Configuration   m_Config;
	Preferences 	m_preferences;

	// Rotator
	bool            m_bWasRunning;
	bool            m_bisAtHome;
	volatile enum Seeks	m_seekMode = NOT_MOVING;
	bool            m_bSetToHomeAzimuth;
	bool            m_bDoStepsPerRotation;

	float           m_fStepsPerDegree;
	StopWatch       m_MoveOffUntilTimer;
	unsigned long   m_nMOVE_OFFUntilLapse = 2000;
	int             m_nMoveDirection;

	volatile long	m_nStepsAtHome = 0;
	volatile long	m_nHomePosEdgePass1 = 0;
	volatile long	m_nHomePosEdgePass2;
	volatile bool	m_HomeFound = false;

	// Utility
	void 			LoadConfig();
	volatile bool	m_bIsSafe = true;
	bool				m_bDoSave;
};



RotatorClass::RotatorClass()
{

	m_seekMode = NOT_MOVING;
	m_bWasRunning = false;
	m_bisAtHome = false;
	m_HomeFound = false;
	m_bSetToHomeAzimuth = false;
	m_bDoStepsPerRotation = false;
	m_nMoveDirection = MOVE_NONE;

	// input
	pinMode(HOME_PIN,               INPUT_PULLUP);
	pinMode(BUTTON_CCW,             INPUT_PULLUP);
	pinMode(BUTTON_CW,              INPUT_PULLUP);
	pinMode(CONDITION_SENSOR_PIN,        INPUT_PULLUP);
	pinMode(SPARE1,    				INPUT);
	pinMode(SPARE2,    				INPUT_PULLUP);

	// output
	pinMode(STEP_PIN,               OUTPUT);
	pinMode(DIRECTION_PIN,          OUTPUT);
	pinMode(STEPPER_ENABLE_PIN,     OUTPUT);
	pinMode(SPARE_OUT1,     		OUTPUT);
	pinMode(SPARE_OUT2,     		OUTPUT);

	LoadConfig();

	m_bDoSave = false;  // we just read the config, no need to resave all the value we're setting
	engine.init();
	stepper = engine.stepperConnectToPin(STEP_PIN);
	stepper->setEnablePin(STEPPER_ENABLE_PIN);
	stepper->setAutoEnable(true);

	SetMaxSpeed(m_Config.maxSpeed);
	SetAcceleration(m_Config.acceleration);
	SetReversed(m_Config.reversed);
	SetStepsPerRotation(m_Config.stepsPerRotation);
	m_bDoSave = true;

	if (digitalRead(CONDITION_SENSOR_PIN) == LOW) {
		m_bIsSafe = false;
	}
	else {
		m_bIsSafe = true;
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
	// reset all timers
	m_MoveOffUntilTimer.reset();
}


void IRAM_ATTR RotatorClass::homeInterrupt()
{
	long  nPos;

	nPos = stepper->getCurrentPosition(); // read position immediately

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


void IRAM_ATTR RotatorClass::conditionsInterrupt()
{
	if (digitalRead(CONDITION_SENSOR_PIN) == LOW) {
		m_bIsSafe = false;
	}
	else
		m_bIsSafe = true;
}


void RotatorClass::LoadConfig()
{
	bool nvsInitDone = false;
	DBPrintln("RotatorClass::LoadConfig");

	m_preferences.begin("RTI_Dome", false);
	nvsInitDone = m_preferences.isKey("nvsInit");
	if(!nvsInitDone) {
		DBPrintln("Initializing NVS");
		m_preferences.end();
		nvs_flash_erase();
		nvs_flash_init();
		m_preferences.begin("RTI_Dome", false);
		m_preferences.putBool("nvsInit", true);

	}
	m_Config.stepsPerRotation = m_preferences.getLong("stepsPerRot",STEPS_DEFAULT);
	m_Config.acceleration = m_preferences.getLong("acceleration",ACCELERATION);
	m_Config.maxSpeed = m_preferences.getLong("maxSpeed",MAX_SPEED);
	m_Config.reversed = m_preferences.getBool("reversed", false);
	m_Config.homeAzimuth = m_preferences.getFloat("homeAzimuth", 0.0f);
	m_Config.parkAzimuth = m_preferences.getFloat("parkAzimuth", 0.0f);
	m_Config.conditionsAction = m_preferences.getInt("condAction", DO_NOTHING);

	m_Config.ipConfig.bUseDHCP = m_preferences.getBool("bUseDHCP", true);
	m_Config.ipConfig.ip.fromString(m_preferences.getString("ip","192.168.0.99"));
	m_Config.ipConfig.dns.fromString(m_preferences.getString("dns","192.168.0.1"));
	m_Config.ipConfig.gateway.fromString(m_preferences.getString("gateway","192.168.0.1"));
	m_Config.ipConfig.subnetMask.fromString(m_preferences.getString("subnetMask","255.255.255.0"));
#ifdef USE_WIFI
	m_Config.wifiIpConfig.ip.fromString(m_preferences.getString("wifi_ip","172.31.255.1"));
	m_Config.wifiIpConfig.sSSID = m_preferences.getString("AP_SSID", "RTIShutter");
	m_Config.wifiIpConfig.sPassword = m_preferences.getString("AP_Password", "RTIShutter");
#endif // USE_WIFI

	DBPrintln("maxSpeed          : " + String(m_Config.maxSpeed));
	DBPrintln("acceleration      : " + String(m_Config.acceleration));
	DBPrintln("stepsPerRotation  : " + String(m_Config.stepsPerRotation));
	DBPrintln("reversed          : " + String(m_Config.reversed));
	DBPrintln("homeAzimuth       : " + String(m_Config.homeAzimuth));
	DBPrintln("parkAzimuth       : " + String(m_Config.parkAzimuth));
	DBPrintln("conditionsAction  : " + String(m_Config.conditionsAction));
	DBPrintln("ipConfig.bUseDHCP : " + String(m_Config.ipConfig.bUseDHCP?"Yes":"No"));
	DBPrintln("ipConfig.ip       : " + IpAddress2String(m_Config.ipConfig.ip));
	DBPrintln("ipConfig.dns      : " + IpAddress2String(m_Config.ipConfig.dns));
	DBPrintln("ipConfig.gateway  : " + IpAddress2String(m_Config.ipConfig.gateway));
	DBPrintln("ipConfig.subnet   : " + IpAddress2String(m_Config.ipConfig.subnetMask));
#ifdef USE_WIFI
	DBPrintln("wifiIpConfig.ip        : " + IpAddress2String(m_Config.wifiIpConfig.ip));
	DBPrintln("wifiIpConfig.sSSID     : " + m_Config.wifiIpConfig.sSSID);
	DBPrintln("wifiIpConfig.sPassword : " + m_Config.wifiIpConfig.sPassword);
#endif
	m_preferences.end();
}

void RotatorClass::getIpConfig(IPConfig &config)
{
	config.bUseDHCP = m_Config.ipConfig.bUseDHCP;
	config.ip = m_Config.ipConfig.ip;
	config.dns = m_Config.ipConfig.dns;
	config.gateway = m_Config.ipConfig.gateway;
	config.subnetMask = m_Config.ipConfig.subnetMask;
}


bool RotatorClass::getDHCPFlag()
{
	return m_Config.ipConfig.bUseDHCP;
}

void RotatorClass::setDHCPFlag(bool bUseDHCP)
{
	m_Config.ipConfig.bUseDHCP = bUseDHCP;
	DBPrintln("New bUseDHCP : " + bUseDHCP?"Yes":"No");
	m_preferences.begin("RTI_Dome", false);
	m_preferences.putBool("bUseDHCP", bUseDHCP);
	m_preferences.end();
}

String RotatorClass::getIPAddress()
{
	return IpAddress2String(m_Config.ipConfig.ip);
}

void RotatorClass::setIPAddress(String ipAddress)
{
	m_Config.ipConfig.ip.fromString(ipAddress);
	DBPrintln("New IP address : " + IpAddress2String(m_Config.ipConfig.ip));
	m_preferences.begin("RTI_Dome", false);
	m_preferences.putString("ip", ipAddress);
	m_preferences.end();
}

String RotatorClass::getIPSubnetMask()
{
	return IpAddress2String(m_Config.ipConfig.subnetMask);
}

void RotatorClass::setIPSubnetMask(String subnetMask)
{
	m_Config.ipConfig.subnetMask.fromString(subnetMask);
	DBPrintln("New subnet mask : " + subnetMask);
	m_preferences.begin("RTI_Dome", false);
	m_preferences.putString("subnetMask", subnetMask);
	m_preferences.end();
}

String RotatorClass::getIPGateway()
{
	return IpAddress2String(m_Config.ipConfig.gateway);
}

void RotatorClass::setIPGateway(String ipGateway)
{
	m_Config.ipConfig.gateway.fromString(ipGateway);
	DBPrintln("New gateway : " + ipGateway);
	// setting DNS IP to gateway IP as we don't use it and this is probably correct for most home users
	m_Config.ipConfig.dns.fromString(ipGateway);
	m_preferences.begin("RTI_Dome", false);
	m_preferences.putString("gateway", ipGateway);
	m_preferences.end();
}

#ifdef USE_WIFI
String RotatorClass::getSSID()
{
	return m_Config.wifiIpConfig.sSSID;
}

void RotatorClass::setSSID(String sSSID)
{
	m_Config.wifiIpConfig.sSSID = sSSID;
	m_preferences.begin("RTI_Dome", false);
	m_preferences.putString("AP_SSID", sSSID);
	m_preferences.end();
}


void RotatorClass::getWiFiConfig(WIFIConfig &config)
{
	config.ip = m_Config.wifiIpConfig.ip;
	config.sSSID = m_Config.wifiIpConfig.sSSID;
	config.sPassword = m_Config.wifiIpConfig.sPassword;
}
#endif

void RotatorClass::setWifiDefault()
{
	DBPrintln("Resseting WiFi to default SSID and IP");

	m_Config.wifiIpConfig.ip.fromString("172.31.255.1");
	m_Config.wifiIpConfig.sSSID = "RTIShutter";
	m_Config.wifiIpConfig.sPassword = "RTIShutter";

	m_preferences.begin("RTI_Dome", false);
	m_preferences.putString("wifi_ip","172.31.255.1");
	m_preferences.putString("AP_SSID","RTIShutter");
	m_preferences.putString("AP_Password","RTIShutter");
	m_preferences.end();
}

String RotatorClass::IpAddress2String(const IPAddress& ipAddress)
{
  return String(ipAddress[0]) + String(".") +
  		String(ipAddress[1]) + String(".") +
		String(ipAddress[2]) + String(".") +
		String(ipAddress[3]);
}

//
// conditions sensor methods
//
bool RotatorClass::GetConditionsStatus()
{
	if (digitalRead(CONDITION_SENSOR_PIN) == LOW) {
		m_bIsSafe = false;
	}
	else
		m_bIsSafe = true;

	return m_bIsSafe;
}

inline int RotatorClass::GetConditionsAction()
{
	return m_Config.conditionsAction;
}

inline void RotatorClass::SetConditionsAction(const int value)
{
	m_Config.conditionsAction = value;
	m_preferences.begin("RTI_Dome", false);
	m_preferences.putInt("condAction", value);
	m_preferences.end();
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

	stepper->setAcceleration(m_Config.acceleration);    //  steps/s²

	if(m_bDoSave) {
		m_preferences.begin("RTI_Dome", false);
		m_preferences.putLong("acceleration", newAccel);
		m_preferences.end();
	}
}

long RotatorClass::GetMaxSpeed()
{
	return m_Config.maxSpeed;
}

void RotatorClass::SetMaxSpeed(const long newSpeed)
{
	m_Config.maxSpeed = newSpeed;
	stepper->setSpeedInHz(m_Config.maxSpeed);  //  steps/s
	if(m_bDoSave) {
		m_preferences.begin("RTI_Dome", false);
		m_preferences.putLong("maxSpeed", newSpeed);
		m_preferences.end();
	}
}

long RotatorClass::GetPosition()
{
	/// Return change in steps relative to
	/// last sync position
	long position;
	position = stepper->getCurrentPosition();

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
	float azimuth = 0.0f;
	long currentPosition = 0;

	currentPosition = GetPosition();
	azimuth = (float)currentPosition / (float)m_Config.stepsPerRotation * 360.0f;

	return float(azimuth);
}

long RotatorClass::GetAzimuthToPosition(const float azimuth)
{
	long newPosition;

	newPosition = (float)m_Config.stepsPerRotation / 360.0f * azimuth;

	return newPosition;
}

void RotatorClass::SyncPosition(const float newAzimuth)
{
	long newPosition;

	newPosition = GetAzimuthToPosition(newAzimuth);
	stepper->setCurrentPosition(newPosition);
}

void RotatorClass::GoToAzimuth(const float newHeading)
{
	// Goto new target
	float currentHeading;
	float delta;

	currentHeading = GetAzimuth();
	delta = GetAngularDistance(currentHeading, newHeading) *  m_fStepsPerDegree;
	m_seekMode = MOVING_GOTO;
	MoveRelative(long(delta));
}

bool RotatorClass::GetReversed()
{
	return m_Config.reversed;
}

void RotatorClass::SetReversed(const bool isReversed)
{
	m_Config.reversed = isReversed;
	stepper->setDirectionPin(DIRECTION_PIN,(!isReversed));
	if(m_bDoSave) {
		m_preferences.begin("RTI_Dome", false);
		m_preferences.putBool("reversed", isReversed);
		m_preferences.end();
	}
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
	long foo;
	m_fStepsPerDegree = (float)newCount / 360.0f;
	m_Config.stepsPerRotation = newCount;
	if(m_bDoSave) {
		m_preferences.begin("RTI_Dome", false);
		m_preferences.putLong("stepsPerRot", newCount);
		m_preferences.end();
	}
}

void RotatorClass::restoreDefaultMotorSettings()
{
	SetMaxSpeed(MAX_SPEED);
	SetAcceleration(ACCELERATION);
	SetStepsPerRotation(STEPS_DEFAULT);
}

float RotatorClass::GetAngularDistance(const float fromAngle, const float toAngle)
{
	float delta;
	delta = toAngle - fromAngle;
	if (delta == 0.0)
		return 0; //  we are already there

	if (delta > 180.0f)
		delta -= 360.0f;

	if (delta < -180.0f)
		delta += 360.0f;

	return delta;
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
	m_preferences.begin("RTI_Dome", false);
	m_preferences.putFloat("homeAzimuth", newHome);
	m_preferences.end();
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
	m_preferences.begin("RTI_Dome", false);
	m_preferences.putFloat("parkAzimuth", newPark);
	m_preferences.end();
}

int RotatorClass::GetSeekMode()
{
	return m_seekMode;
}


//
// Homing and Calibration
//
void RotatorClass::StartHoming()
{
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
	stepper->setCurrentPosition(0);
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
				if (!stepper->isRunning()) {
					m_seekMode = CALIBRATION_STEP1;
					stepper->setCurrentPosition(0);
					MoveRelative(160000000L);
				}
				break;

			case(CALIBRATION_MOVE_OFF2):
				if(m_MoveOffUntilTimer.elapsed() >= m_nMOVE_OFFUntilLapse) {
					m_seekMode = CALIBRATION_MEASURE;
				}
				break;

			case(CALIBRATION_MEASURE):
				if (!stepper->isRunning()) { // we have to wait for it to have stopped
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

void RotatorClass::MoveRelative(const long howFar)
{
	// Use by Home and Calibrate
	// Tells dome to rotate more than 360 degrees
	// from current position. Stopped only by
	// homing or calibrating routine.

	m_nMoveDirection = MOVE_NEGATIVE;
	if (howFar > 0)
		m_nMoveDirection = MOVE_POSITIVE;
	else if(howFar == 0 ) {
		m_nMoveDirection = MOVE_NONE;
		m_seekMode = NOT_MOVING;
		return;
		}
	m_bisAtHome = false;

	motorMoveRelative(howFar);
}


void IRAM_ATTR RotatorClass::ButtonCheck()
{
	if (digitalRead(BUTTON_CW) == LOW) {
		MoveRelative(160000000L);
	}
	else if (digitalRead(BUTTON_CCW) == LOW)  {
		MoveRelative(-160000000L);
	}
	else {
		motorStop();
	}
}

void RotatorClass::Run()
{
	long stepsFromZero;
	long position;
	float azimuthDelta;

	if (m_seekMode > HOMING_HOME)
		Calibrate();

	if (stepper->isRunning()) {
		m_bWasRunning = true;
		if (m_seekMode == HOMING_HOME && m_HomeFound) { // We're looking for home and found it
			motorStop();
			m_bSetToHomeAzimuth = true; // Need to set home az but not until rotator is stopped;
			m_seekMode = HOMING_FINISH;
			return;
		}
		return;
	}

	if( m_seekMode == HOMING_BACK_HOME) {
		m_bisAtHome = true; // we're back home and done homing.
		m_seekMode = NOT_MOVING;
	}

	if (m_bDoStepsPerRotation) {
		m_bDoStepsPerRotation = false;
		SetStepsPerRotation(m_nHomePosEdgePass2 - m_nHomePosEdgePass1);
		position = stepper->getCurrentPosition();
		azimuthDelta = (float)(position - m_nHomePosEdgePass2) / m_fStepsPerDegree;
		SyncPosition(azimuthDelta + m_Config.homeAzimuth);
		m_nStepsAtHome = 0;
	}

	if (m_bSetToHomeAzimuth) {
		m_bSetToHomeAzimuth = false;
		position = stepper->getCurrentPosition();
		azimuthDelta = (float)(position - m_nStepsAtHome) / m_fStepsPerDegree;
		SyncPosition(azimuthDelta + m_Config.homeAzimuth);
		position = stepper->getCurrentPosition();
		GoToAzimuth(m_Config.homeAzimuth); // moving to home now that we know where we are
		m_seekMode = HOMING_BACK_HOME;
	}

	if (m_bWasRunning) {
		stepsFromZero = GetPosition();
		if (stepsFromZero < 0) {
			while (stepsFromZero < 0)
				stepsFromZero += m_Config.stepsPerRotation;

			stepper->setCurrentPosition(stepsFromZero);
		}

		if (stepsFromZero > m_Config.stepsPerRotation) {
			while (stepsFromZero > m_Config.stepsPerRotation)
				stepsFromZero -= m_Config.stepsPerRotation;

			stepper->setCurrentPosition(stepsFromZero);
		}

		if( m_seekMode == NOT_MOVING) {
			// not moving anymore ..
			m_nMoveDirection = MOVE_NONE;
			m_bWasRunning = false;
			// check if we stopped on the home sensor
			if(digitalRead(HOME_PIN) == LOW) {
				// we're at the home position
				m_bisAtHome = true;
			}
			position = stepper->getCurrentPosition();
			while (position >= m_Config.stepsPerRotation)
				position -= m_Config.stepsPerRotation;

			while (position < 0)
				position += m_Config.stepsPerRotation;

			if(position == (m_Config.stepsPerRotation -1))
				position = 0;
			stepper->setCurrentPosition(position);
		}

		if(m_seekMode == MOVING_GOTO) {
			m_nMoveDirection = MOVE_NONE;
			m_seekMode = NOT_MOVING;
			position = stepper->getCurrentPosition();
			while (position >= m_Config.stepsPerRotation)
				position -= m_Config.stepsPerRotation;

			while (position < 0)
				position += m_Config.stepsPerRotation;

			if(position == (m_Config.stepsPerRotation -1))
				position = 0;
			stepper->setCurrentPosition(position);
		}
	} // end if (m_bWasRunning)
}

void RotatorClass::Stop()
{
	m_seekMode = NOT_MOVING;
	stepper->forceStop();
}



void RotatorClass::motorStop()
{
	stepper->stopMove();
}


void RotatorClass::motorMoveRelative(const long howFar)
{
	stepper->move(howFar);
}	


bool RotatorClass::checkBoundaries(float dTargetAz, float dDomeAz, float dMargin)
{
	float highMark;
	float lowMark;
	float roundedTargetAz;

	// we need to test "large" depending on the heading error and movement coasting
	highMark = ceil(dDomeAz)+dMargin;
	lowMark = ceil(dDomeAz)-dMargin;
	roundedTargetAz = ceil(dTargetAz);

	if(lowMark < 0.0f && highMark > 0.0f) { // we're close to 0 degre but above 0
		if((roundedTargetAz + 2.0f) >= 360.0f)
			roundedTargetAz = (roundedTargetAz + 2.0f) - 360.0f;
		if ( (roundedTargetAz > lowMark) && (roundedTargetAz <= highMark)) {
			return true;
		}
	}
	if ( lowMark > 0.0f && highMark>360.0f ) { // we're close to 0 but from the other side
		if( (roundedTargetAz + 360.0f) > lowMark && (roundedTargetAz + 360.0f) <= highMark) {
			return true;
		}
	}
	if (roundedTargetAz > lowMark && roundedTargetAz <= highMark) {
		return true;
	}

	return false;
}
