//
//  RTI-Dome.h
//  RTI-Dome
//
//  Created by Rodolphe Pineau on 2020/10/4.
//  RTI-Dome X2 plugin

#ifndef __RTI_Dome__
#define __RTI_Dome__

// standard C includes
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <memory.h>
#include <math.h>
#include <string.h>
#include <time.h>
#ifdef SB_MAC_BUILD
#include <unistd.h>
#endif
// C++ includes
#include <string>
#include <vector>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <chrono>
#include <thread>
#include <ctime>

// SB includes
#include "../../licensedinterfaces/driverrootinterface.h"
#include "../../licensedinterfaces/sberrorx.h"
#include "../../licensedinterfaces/serxinterface.h"

#include "StopWatch.h"

#define MAKE_ERR_CODE(P_ID, DTYPE, ERR_CODE)  (((P_ID<<24) & 0xff000000) | ((DTYPE<<16) & 0x00ff0000)  | (ERR_CODE & 0x0000ffff))

#define SERIAL_BUFFER_SIZE 256
#define MAX_TIMEOUT 500
#define MAX_READ_WAIT_TIMEOUT 25
#define NB_RX_WAIT 10
#define ND_LOG_BUFFER_SIZE 256
#define PANID_TIMEOUT 15    // in seconds
#define RAIN_CHECK_INTERVAL 10

#define PLUGIN_VERSION      1.34
#define PLUGIN_ID   1

// #define PLUGIN_DEBUG 2


// Rotator commands
const char ABORT_MOVE_CMD               = 'a'; // Tell everything to STOP!
const char ETH_RECONFIG                 = 'b'; // reconfigure ethernet
const char CALIBRATE_ROTATOR_CMD        = 'c'; // Calibrate the dome
const char RESTORE_MOTOR_DEFAULT        = 'd'; // restore default values for motor control.
const char ACCELERATION_ROTATOR_CMD     = 'e'; // Get/Set stepper acceleration
const char ETH_MAC_ADDRESS              = 'f'; // get the MAC address.
const char GOTO_ROTATOR_CMD             = 'g'; // Get dome azimuth or move to new position (GoT
const char HOME_ROTATOR_CMD             = 'h'; // Home the dome
const char HOMEAZ_ROTATOR_CMD           = 'i'; // Get/Set home position
const char IP_ADDRESS                   = 'j'; // get/set the IP address
const char VOLTS_ROTATOR_CMD            = 'k'; // Get volts and get/set cutoff
const char PARKAZ_ROTATOR_CMD           = 'l'; // Get/Set park azimuth
const char SLEW_ROTATOR_GET             = 'm'; // Get Slewing status/direction
const char RAIN_ROTATOR_ACTION          = 'n'; // Get/Set action when rain sensor triggered (do nothing, home, park)
const char IS_SHUTTER_PRESENT           = 'o'; // check if the shutter has responded to pings
const char IP_SUBNET                    = 'p'; // get/set the ip subnet
const char PANID_GET                    = 'q'; // get and set the XBEE PAN ID
const char SPEED_ROTATOR_CMD            = 'r'; // Get/Set step rate (speed)
const char SYNC_ROTATOR_CMD             = 's'; // Sync to new Azimuth
const char STEPSPER_ROTATOR_CMD         = 't'; // Get/set Steps per rotation
const char IP_GATEWAY                   = 'u'; // get/set default gateway IP
const char VERSION_ROTATOR_GET          = 'v'; // Get Firmware Version
const char IP_DHCP                      = 'w'; // get/set DHCP mode
                                        //'x' see bellow
const char REVERSED_ROTATOR_CMD         = 'y'; // Get/Set stepper reversed status
const char HOMESTATUS_ROTATOR_GET       = 'z'; // Get homed status

const char RAIN_SHUTTER_GET             = 'F'; // Get rain status (from client) or tell shutter it's raining (from Rotator)

const char INIT_XBEE                    = 'x'; // force a XBee reconfig

// available A B J N S U W X Z
// Shutter commands
const char CLOSE_SHUTTER_CMD            = 'C'; // Close shutter
const char SHUTTER_RESTORE_MOTOR_DEFAULT= 'D'; // Restore default values for motor control.
const char ACCELERATION_SHUTTER_CMD     = 'E'; // Get/Set stepper acceleration
                                       // 'F' see above
//const char ELEVATION_SHUTTER_CMD      = 'G'; // Get/Set altitude TBD
const char HELLO_CMD                    = 'H'; // Let shutter know we're here
const char WATCHDOG_INTERVAL_SET        = 'I'; // Tell shutter when to trigger the watchdog for communication loss with rotator
const char VOLTS_SHUTTER_CMD            = 'K'; // Get volts and set cutoff voltage (close if bellow)
const char SHUTTER_PING                 = 'L'; // Shutter ping, uses to reset watchdog timer.
const char STATE_SHUTTER_GET            = 'M'; // Get shutter state
const char OPEN_SHUTTER_CMD             = 'O'; // Open the shutter
const char POSITION_SHUTTER_GET            = 'P'; // Get step position
const char SHUTTER_PANID_GET            = 'Q'; // get and set the XBEE PAN ID
const char SPEED_SHUTTER_CMD            = 'R'; // Get/Set step rate (speed)
const char STEPSPER_SHUTTER_CMD         = 'T'; // Get/Set steps per stroke
const char VERSION_SHUTTER_GET          = 'V'; // Get version string
const char REVERSED_SHUTTER_CMD         = 'Y'; // Get/Set stepper reversed status
const char SOUTH_WALL_PRESENT           = 'Z'; // enable/disable south wall operations

// Error code
enum RTIDomeErrors {PLUGIN_OK=0, NOT_CONNECTED, CANT_CONNECT, BAD_CMD_RESPONSE, COMMAND_FAILED, COMMAND_TIMEOUT, ERR_RAINING, ERR_BATTERY_LOW};
enum RTIDomeShutterState { OPEN=0 , CLOSED, OPENING, CLOSING, BOTTOM_OPEN, BOTTOM_CLOSED, BOTTOM_OPENING, BOTTOM_CLOSING, SHUTTER_ERROR, FINISHING_OPEN, FINISHING_CLOSE };

enum HomeStatuses {NOT_AT_HOME = 0, HOMED, ATHOME};
enum RainActions {DO_NOTHING=0, HOME, PARK};
enum MoveDirection {MOVE_NEGATIVE = -1, MOVE_NONE, MOVE_POSITIVE};
// RG-11
enum RainSensorStates {RAINING= 0, NOT_RAINING, RAIN_UNKNOWN};

class CRTIDome
{
public:
    CRTIDome();
    ~CRTIDome();

    int         Connect(const char *pszPort);
    void        Disconnect(void);
    const bool  IsConnected(void) { return m_bIsConnected; }

    void        setSerxPointer(SerXInterface *p) { m_pSerx = p; }

    // Dome commands
    int syncDome(double dAz, double dEl);
    int parkDome(void);
    int unparkDome(void);
    int gotoAzimuth(double dNewAz);
    int openShutter();
    int closeShutter();
    int getFirmwareVersion(std::string &sVersion, float &fVersion);
    int getFirmwareVersion(float &fVersion);
    int getShutterFirmwareVersion(std::string &sVersion, float &fVersion);
    int goHome();
    int calibrate();

    // command complete functions
    int isGoToComplete(bool &bComplete);
    int isOpenComplete(bool &bComplete);
    int isCloseComplete(bool &bComplete);
    int isParkComplete(bool &bComplete);
    int isUnparkComplete(bool &bComplete);
    int isFindHomeComplete(bool &bComplete);
    int isCalibratingComplete(bool &bComplete);

    int abortCurrentCommand();
    int sendShutterHello();
    int getShutterPresent(bool &bShutterPresent);
    // getter/setter
    int getNbTicksPerRev();
    int setNbTicksPerRev(int nSteps);

    int getBatteryLevel();

    double getHomeAz();
    int setHomeAz(double dAz);

    double getParkAz();
    int setParkAz(double dAz);

    double getCurrentAz();
    double getCurrentEl();

    int getBatteryLevels(double &domeVolts, double &dDomeCutOff, double &dShutterVolts, double &dShutterCutOff);
    int setBatteryCutOff(double dDomeCutOff, double dShutterCutOff);

    int getDefaultDir(bool &bNormal);
    int setDefaultDir(bool bNormal);

    int getRainSensorStatus(int &nStatus);

    int getRotationSpeed(int &nSpeed);
    int setRotationSpeed(int nSpeed);

    int getRotationAcceleration(int &nAcceleration);
    int setRotationAcceleration(int nAcceleration);

    int getShutterSpeed(int &nSpeed);
    int setShutterSpeed(int nSpeed);

    int getShutterAcceleration(int &nAcceleration);
    int setShutterAcceleration(int nAcceleration);

    void setHomeOnPark(const bool bEnabled);
    void setHomeOnUnpark(const bool bEnabled);

	int	getSutterWatchdogTimerValue(int &nValue);
	int	setSutterWatchdogTimerValue(const int &nValue);

    int getRainAction(int &nAction);
    int setRainAction(const int &nAction);

    int getPanId(int &nPanId);
    int setPanId(const int nPanId);
    int getShutterPanId(int &nPanId);
    int isPanIdSet(const int nPanId, bool &bSet);
    
    int restoreDomeMotorSettings();
    int restoreShutterMotorSettings();
    
    void enableRainStatusFile(bool bEnable);
    void getRainStatusFileName(std::string &fName);
    void writeRainStatus();

    // network config
    int getMACAddress(std::string &MACAddress);
    int reconfigureNetwork();

    int getUseDHCP(bool &bUseDHCP);
    int setUseDHCP(bool bUseDHCP);

    int getIpAddress(std::string &IpAddress);
    int setIpAddress(std::string IpAddress);

    int getSubnetMask(std::string &subnetMask);
    int setSubnetMask(std::string subnetMask);

    int getIPGateway(std::string &IpAddress);
    int setIPGateway(std::string IpAddress);

    
protected:

    int             domeCommand(const std::string sCmd, std::string &sResp, char respCmdCode, int nTimeout = MAX_TIMEOUT, char cEndOfResponse = '#');
    int             readResponse(std::string &sResp, int nTimeout = MAX_TIMEOUT, char cEndOfResponse = '#');

    int             getDomeAz(double &dDomeAz);
    int             getDomeEl(double &dDomeEl);
    int             getDomeHomeAz(double &dAz);
    int             getDomeParkAz(double &dAz);
    int             getShutterState(int &nState);
    int             getDomeStepPerRev(int &nStepPerRev);
    int             setDomeStepPerRev(int nStepPerRev);

    bool            isDomeMoving();
    bool            isDomeAtHome();
    int             parseFields(std::string sResp, std::vector<std::string> &svFields, char cSeparator);

    bool            checkBoundaries(double dTargetAz, double dDomeAz, double nMargin=2.0);

    SerXInterface   *m_pSerx;

    std::string     m_Port;
    bool            m_bNetworkConnected;

    bool            m_bIsConnected;
    bool            m_bParked;
    bool            m_bShutterOpened;
    bool            m_bCalibrating;

    int             m_nNbStepPerRev;
    double          m_dShutterBatteryVolts;
    double          m_dHomeAz;

    double          m_dParkAz;

    double          m_dCurrentAzPosition;
    double          m_dCurrentElPosition;

    double          m_dGotoAz;


    std::string     m_sFirmwareVersion;
    float           m_fVersion;
    std::string     m_sShutterFirmwareVersion;
    float           m_fShutterVersion;

    int             m_nShutterState;
    bool            m_bShutterOnly; // roll off roof so the arduino is running the shutter firmware only.
    int             m_nHomingTries;
    int             m_nGotoTries;
    bool            m_bParking;
    bool            m_bUnParking;
    int             m_nIsRaining;
    bool            m_bHomeOnPark;
    bool            m_bHomeOnUnpark;
    bool            m_bShutterPresent;

    std::string     m_sRainStatusfilePath;
    std::ofstream   m_RainStatusfile;
    bool            m_bSaveRainStatus;
    int             m_nRainStatus;
    CStopWatch      m_cRainCheckTimer;
    
    std::string     m_IpAddress;
    std::string     m_SubnetMask;
    std::string     m_GatewayIP;
    bool            m_bUseDHCP;
    
#ifdef PLUGIN_DEBUG
    // timestamp for logs
    const std::string getTimeStamp();
    std::ofstream m_sLogFile;
    std::string m_sLogfilePath;
#endif

};

#endif
