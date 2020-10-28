//
//  RTI-Dome.cpp
//  RTI-Dome X2 plugin
//
//  Created by Rodolphe Pineau on 2020/10/4.


#include "RTI-Dome.h"

CRTIDome::CRTIDome()
{
    // set some sane values
    m_pSerx = NULL;
    m_bIsConnected = false;

    m_nNbStepPerRev = 0;
    m_dShutterBatteryVolts = 0.0;

    m_dHomeAz = 0;
    m_dParkAz = 0;

    m_dCurrentAzPosition = 0.0;
    m_dCurrentElPosition = 0.0;

    m_bCalibrating = false;
    m_bParking = false;
    m_bUnParking = false;

    m_bShutterOpened = false;

    m_bParked = true;
    m_bHomed = false;

    m_fVersion = 0.0;
    m_nHomingTries = 0;
    m_nGotoTries = 0;

    m_nIsRaining = NOT_RAINING;
    m_bSaveRainStatus = false;
    RainStatusfile = NULL;
    m_cRainCheckTimer.Reset();

    m_bHomeOnPark = false;
    m_bHomeOnUnpark = false;

    m_bShutterPresent = false;
    
    
    memset(m_szFirmwareVersion,0,SERIAL_BUFFER_SIZE);
    memset(m_szLogBuffer,0,ND_LOG_BUFFER_SIZE);

#ifdef PLUGIN_DEBUG
#if defined(SB_WIN_BUILD)
    m_sLogfilePath = getenv("HOMEDRIVE");
    m_sLogfilePath += getenv("HOMEPATH");
    m_sLogfilePath += "\\RTI-Dome-Log.txt";
#elif defined(SB_LINUX_BUILD)
    m_sLogfilePath = getenv("HOME");
    m_sLogfilePath += "/RTI-Dome-Log.txt";
#elif defined(SB_MAC_BUILD)
    m_sLogfilePath = getenv("HOME");
    m_sLogfilePath += "/RTI-Dome-Log.txt";
#endif
    Logfile = fopen(m_sLogfilePath.c_str(), "w");
#endif

#if defined(SB_WIN_BUILD)
    m_sRainStatusfilePath = getenv("HOMEDRIVE");
    m_sRainStatusfilePath += getenv("HOMEPATH");
    m_sRainStatusfilePath += "\\RTI_Rain.txt";
#elif defined(SB_LINUX_BUILD)
    m_sRainStatusfilePath = getenv("HOME");
    m_sRainStatusfilePath += "/RTI_Rain.txt";
#elif defined(SB_MAC_BUILD)
    m_sRainStatusfilePath = getenv("HOME");
    m_sRainStatusfilePath += "/RTI_Rain.txt";
#endif
    
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    ltime = time(NULL);
    timestamp = asctime(localtime(&ltime));
    timestamp[strlen(timestamp) - 1] = 0;
    fprintf(Logfile, "[%s] [CRTIDome] Version %3.2f build 2019_12_06_1810.\n", timestamp, DRIVER_VERSION);
    fprintf(Logfile, "[%s] [CRTIDome] Constructor Called.\n", timestamp);
    fprintf(Logfile, "[%s] [CRTIDome] Rains status file : '%s'.\n", timestamp, m_sRainStatusfilePath.c_str());
    fflush(Logfile);
#endif

}

CRTIDome::~CRTIDome()
{
#ifdef	PLUGIN_DEBUG
    // Close LogFile
    if (Logfile) fclose(Logfile);
#endif
    if(RainStatusfile) {
        fclose(RainStatusfile);
        RainStatusfile = NULL;
    }
}

int CRTIDome::Connect(const char *pszPort)
{
    int nErr;
    bool bDummy;
    
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    ltime = time(NULL);
    timestamp = asctime(localtime(&ltime));
    timestamp[strlen(timestamp) - 1] = 0;
    fprintf(Logfile, "[%s] CRTIDome::Connect Called %s\n", timestamp, pszPort);
    fflush(Logfile);
#endif

    // 9600 8N1
    nErr = m_pSerx->open(pszPort, 115200, SerXInterface::B_NOPARITY, "-DTR_CONTROL 1");
    if(nErr) {
        m_bIsConnected = false;
        return nErr;
    }
    m_bIsConnected = true;
    m_bCalibrating = false;
    m_bUnParking = false;
    m_bHomed = false;

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    ltime = time(NULL);
    timestamp = asctime(localtime(&ltime));
    timestamp[strlen(timestamp) - 1] = 0;
    fprintf(Logfile, "[%s] CRTIDome::Connect connected to %s\n", timestamp, pszPort);
    fflush(Logfile);
#endif

    // the arduino take over a second to start as it need to init the XBee and there is 1100 ms pause in the code :(
    if(m_pSleeper)
        m_pSleeper->sleep(2000);

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    ltime = time(NULL);
    timestamp = asctime(localtime(&ltime));
    timestamp[strlen(timestamp) - 1] = 0;
    fprintf(Logfile, "[%s] CRTIDome::Connect Getting Firmware\n", timestamp);
    fflush(Logfile);
#endif

    // if this fails we're not properly connected.
    nErr = getFirmwareVersion(m_szFirmwareVersion, SERIAL_BUFFER_SIZE);
    if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        ltime = time(NULL);
        timestamp = asctime(localtime(&ltime));
        timestamp[strlen(timestamp) - 1] = 0;
        fprintf(Logfile, "[%s] [CRTIDome::Connect] Error Getting Firmware.\n", timestamp);
        fflush(Logfile);
#endif
        m_bIsConnected = false;
        m_pSerx->close();
        return FIRMWARE_NOT_SUPPORTED;
    }

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    ltime = time(NULL);
    timestamp = asctime(localtime(&ltime));
    timestamp[strlen(timestamp) - 1] = 0;
    fprintf(Logfile, "[%s] CRTIDome::Connect Got Firmware %s ( %f )\n", timestamp, m_szFirmwareVersion, m_fVersion);
    fflush(Logfile);
#endif
    if(m_fVersion < 2.0f && m_fVersion != 0.523f && m_fVersion != 0.522f)  {
        return FIRMWARE_NOT_SUPPORTED;
    }

    nErr = getDomeParkAz(m_dCurrentAzPosition);
    if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        ltime = time(NULL);
        timestamp = asctime(localtime(&ltime));
        timestamp[strlen(timestamp) - 1] = 0;
        fprintf(Logfile, "[%s] CRTIDome::Connect getDomeParkAz nErr : %d\n", timestamp, nErr);
        fflush(Logfile);
#endif
        return nErr;
    }
    nErr = getDomeHomeAz(m_dHomeAz);
    if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        ltime = time(NULL);
        timestamp = asctime(localtime(&ltime));
        timestamp[strlen(timestamp) - 1] = 0;
        fprintf(Logfile, "[%s] CRTIDome::Connect getDomeHomeAz nErr : %d\n", timestamp, nErr);
        fflush(Logfile);
#endif
        return nErr;
    }

    sendShutterHello();
    m_pSleeper->sleep(250);
    getShutterPresent(bDummy);

    return SB_OK;
}


void CRTIDome::Disconnect()
{
    if(m_bIsConnected) {
        abortCurrentCommand();
        m_pSerx->purgeTxRx();
        m_pSerx->close();
    }
    m_bIsConnected = false;
    m_bCalibrating = false;
    m_bUnParking = false;
    m_bHomed = false;

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    ltime = time(NULL);
    timestamp = asctime(localtime(&ltime));
    timestamp[strlen(timestamp) - 1] = 0;
    fprintf(Logfile, "[%s] [CRTIDome::Disconnect] m_bIsConnected = %d\n", timestamp, m_bIsConnected);
    fflush(Logfile);
#endif
}


int CRTIDome::domeCommand(const char *pszCmd, char *pszResult, char respCmdCode, int nResultMaxLen, int nTimeout)
{
    int nErr = ND_OK;
    char szResp[SERIAL_BUFFER_SIZE];
    unsigned long  ulBytesWrite;

    m_pSerx->purgeTxRx();

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    ltime = time(NULL);
    timestamp = asctime(localtime(&ltime));
    timestamp[strlen(timestamp) - 1] = 0;
    fprintf(Logfile, "[%s] CRTIDome::domeCommand sending : %s\n", timestamp, pszCmd);
    fflush(Logfile);
#endif

    nErr = m_pSerx->writeFile((void *)pszCmd, strlen(pszCmd), ulBytesWrite);
    m_pSerx->flushTx();
    if(nErr)
        return nErr;

    if (!respCmdCode)
        return nErr;

    // read response
    nErr = readResponse(szResp, SERIAL_BUFFER_SIZE);
    if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        ltime = time(NULL);
        timestamp = asctime(localtime(&ltime));
        timestamp[strlen(timestamp) - 1] = 0;
        fprintf(Logfile, "[%s] CRTIDome::domeCommand ***** ERROR READING RESPONSE **** error = %d , response : %s\n", timestamp, nErr, szResp);
        fflush(Logfile);
#endif
        return nErr;
    }
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    ltime = time(NULL);
    timestamp = asctime(localtime(&ltime));
    timestamp[strlen(timestamp) - 1] = 0;
    fprintf(Logfile, "[%s] CRTIDome::domeCommand response : %s\n", timestamp, szResp);
    fflush(Logfile);
#endif


    if (szResp[0] != respCmdCode)
        nErr = ND_BAD_CMD_RESPONSE;

    if(pszResult)
        strncpy(pszResult, &szResp[1], nResultMaxLen);

    return nErr;

}

int CRTIDome::readResponse(char *szRespBuffer, int nBufferLen, int nTimeout)
{
    int nErr = ND_OK;
    unsigned long ulBytesRead = 0;
    unsigned long ulTotalBytesRead = 0;
    char *pszBufPtr;

    memset(szRespBuffer, 0, (size_t) nBufferLen);
    pszBufPtr = szRespBuffer;

    do {
        nErr = m_pSerx->readFile(pszBufPtr, 1, ulBytesRead, nTimeout);
        if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
            ltime = time(NULL);
            timestamp = asctime(localtime(&ltime));
            timestamp[strlen(timestamp) - 1] = 0;
            fprintf(Logfile, "[%s] [CRTIDome::readResponse] readFile error\n", timestamp);
            fflush(Logfile);
#endif
            return nErr;
        }

        if (ulBytesRead !=1) {// timeout
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
            ltime = time(NULL);
            timestamp = asctime(localtime(&ltime));
            timestamp[strlen(timestamp) - 1] = 0;
            fprintf(Logfile, "[%s] CRTIDome::readResponse Timeout while waiting for response from controller\n", timestamp);
            fflush(Logfile);
#endif

            nErr = ND_BAD_CMD_RESPONSE;
            break;
        }
        ulTotalBytesRead += ulBytesRead;
    } while (*pszBufPtr++ != '#' && ulTotalBytesRead < nBufferLen );

    if(ulTotalBytesRead)
        *(pszBufPtr-1) = 0; //remove the #

    return nErr;
}


int CRTIDome::getDomeAz(double &dDomeAz)
{
    int nErr = ND_OK;
    char szResp[SERIAL_BUFFER_SIZE];

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(m_bCalibrating)
        return nErr;

    nErr = domeCommand("g#", szResp, 'g', SERIAL_BUFFER_SIZE);
    if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        ltime = time(NULL);
        timestamp = asctime(localtime(&ltime));
        timestamp[strlen(timestamp) - 1] = 0;
        fprintf(Logfile, "[%s] [CRTIDome::getDomeAz] ERROR = %s\n", timestamp, szResp);
        fflush(Logfile);
#endif
        return nErr;
    }
    // convert Az string to double
    dDomeAz = atof(szResp);
    m_dCurrentAzPosition = dDomeAz;

    if(m_cRainCheckTimer.GetElapsedSeconds() > RAIN_CHECK_INTERVAL) {
        writeRainStatus();
        m_cRainCheckTimer.Reset();
    }
    return nErr;
}

int CRTIDome::getDomeEl(double &dDomeEl)
{
    int nErr = ND_OK;
    // char szResp[SERIAL_BUFFER_SIZE];

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(m_bCalibrating)
        return nErr;

    if(!m_bShutterOpened)
    {
        dDomeEl = 0.0;
        return nErr;
    }
    else {
        dDomeEl = 90.0;
        return nErr;
    }

// this doesn't work in firmware 2.x as it's not implemented yet
/*
    nErr = domeCommand("G#", szResp, 'G', SERIAL_BUFFER_SIZE);
    if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        ltime = time(NULL);
        timestamp = asctime(localtime(&ltime));
        timestamp[strlen(timestamp) - 1] = 0;
        fprintf(Logfile, "[%s] [CRTIDome::getDomeEl] ERROR = %s\n", timestamp, szResp);
        fflush(Logfile);
#endif
        return nErr;
    }

    // convert El string to double
    dDomeEl = atof(szResp);
    m_dCurrentElPosition = dDomeEl;

    return nErr;
 */
}


int CRTIDome::getDomeHomeAz(double &dAz)
{
    int nErr = ND_OK;
    char szResp[SERIAL_BUFFER_SIZE];

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(m_bCalibrating)
        return nErr;

    nErr = domeCommand("i#", szResp, 'i', SERIAL_BUFFER_SIZE);
    if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        ltime = time(NULL);
        timestamp = asctime(localtime(&ltime));
        timestamp[strlen(timestamp) - 1] = 0;
        fprintf(Logfile, "[%s] [CRTIDome::getDomeHomeAz] ERROR = %s\n", timestamp, szResp);
        fflush(Logfile);
#endif
        return nErr;
    }

    // convert Az string to double
    dAz = atof(szResp);
    m_dHomeAz = dAz;
    return nErr;
}

int CRTIDome::getDomeParkAz(double &dAz)
{
    int nErr = ND_OK;
    char szResp[SERIAL_BUFFER_SIZE];

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(m_bCalibrating)
        return nErr;

    nErr = domeCommand("l#", szResp, 'l', SERIAL_BUFFER_SIZE);
    if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        ltime = time(NULL);
        timestamp = asctime(localtime(&ltime));
        timestamp[strlen(timestamp) - 1] = 0;
        fprintf(Logfile, "[%s] [CRTIDome::getDomeParkAz] ERROR = %s\n", timestamp, szResp);
        fflush(Logfile);
#endif
        return nErr;
    }

    // convert Az string to double
    dAz = atof(szResp);
    m_dParkAz = dAz;
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        ltime = time(NULL);
        timestamp = asctime(localtime(&ltime));
        timestamp[strlen(timestamp) - 1] = 0;
        fprintf(Logfile, "[%s] [CRTIDome::getDomeParkAz] m_dParkAz = %3.2f\n", timestamp, m_dParkAz);
        fflush(Logfile);
#endif

    return nErr;
}


int CRTIDome::getShutterState(int &nState)
{
    int nErr = ND_OK;
    char szResp[SERIAL_BUFFER_SIZE];
    std::vector<std::string> shutterStateFileds;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(!m_bShutterPresent) {
        nState = SHUTTER_ERROR;
        return nErr;
    }

    if(m_bCalibrating)
        return nErr;

	

    nErr = domeCommand("M#", szResp, 'M', SERIAL_BUFFER_SIZE);
    if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        ltime = time(NULL);
        timestamp = asctime(localtime(&ltime));
        timestamp[strlen(timestamp) - 1] = 0;
        fprintf(Logfile, "[%s] [CRTIDome::getShutterState] ERROR = %s\n", timestamp, szResp);
        fflush(Logfile);
#endif
        nState = SHUTTER_ERROR;
        return nErr;
    }

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    ltime = time(NULL);
    timestamp = asctime(localtime(&ltime));
    timestamp[strlen(timestamp) - 1] = 0;
    fprintf(Logfile, "[%s] [CRTIDome::getShutterState] response = '%s'\n", timestamp, szResp);
    fflush(Logfile);
#endif

    nState = atoi(szResp);

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    ltime = time(NULL);
    timestamp = asctime(localtime(&ltime));
    timestamp[strlen(timestamp) - 1] = 0;
    fprintf(Logfile, "[%s] [CRTIDome::getShutterState] nState = '%d'\n", timestamp, nState);
    fflush(Logfile);
#endif

    return nErr;
}


int CRTIDome::getDomeStepPerRev(int &nStepPerRev)
{
    int nErr = ND_OK;
    char szResp[SERIAL_BUFFER_SIZE];

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    nErr = domeCommand("t#", szResp, 't', SERIAL_BUFFER_SIZE);
    if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        ltime = time(NULL);
        timestamp = asctime(localtime(&ltime));
        timestamp[strlen(timestamp) - 1] = 0;
        fprintf(Logfile, "[%s] [CRTIDome::getDomeStepPerRev] ERROR = %s\n", timestamp, szResp);
        fflush(Logfile);
#endif
        return nErr;
    }

    nStepPerRev = atoi(szResp);
    m_nNbStepPerRev = nStepPerRev;
    return nErr;
}

int CRTIDome::setDomeStepPerRev(int nStepPerRev)
{
    int nErr = ND_OK;
    char szBuf[SERIAL_BUFFER_SIZE];
    char szResp[SERIAL_BUFFER_SIZE];

    m_nNbStepPerRev = nStepPerRev;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    snprintf(szBuf, SERIAL_BUFFER_SIZE, "t%d#", nStepPerRev);
    nErr = domeCommand(szBuf, szResp, 'i', SERIAL_BUFFER_SIZE);
    return nErr;

}

int CRTIDome::getBatteryLevels(double &domeVolts, double &dDomeCutOff, double &dShutterVolts, double &dShutterCutOff)
{
    int nErr = ND_OK;
    int rc = 0;
    char szResp[SERIAL_BUFFER_SIZE];


    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(m_bCalibrating)
        return nErr;
    // Dome
    nErr = domeCommand("k#", szResp, 'k', SERIAL_BUFFER_SIZE);
    if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        ltime = time(NULL);
        timestamp = asctime(localtime(&ltime));
        timestamp[strlen(timestamp) - 1] = 0;
        fprintf(Logfile, "[%s] [CRTIDome::getBatteryLevels] ERROR = %s\n", timestamp, szResp);
        fflush(Logfile);
#endif
        return nErr;
    }

    rc = sscanf(szResp, "%lf,%lf", &domeVolts, &dDomeCutOff);
    if(rc == 0) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        ltime = time(NULL);
        timestamp = asctime(localtime(&ltime));
        timestamp[strlen(timestamp) - 1] = 0;
        fprintf(Logfile, "[%s] [CRTIDome::getBatteryLevels] sscanf ERROR\n", timestamp);
        fflush(Logfile);
#endif
        return COMMAND_FAILED;
    }

    // do a proper conversion as for some reason the value is scaled weirdly ( it's multiplied by 3/2)
    // Arduino ADC convert 0-5V to 0-1023 which is 0.0049V per unit
    if(m_fVersion < 2.0f) {
        domeVolts = (domeVolts*2) * 0.0049f;
        dDomeCutOff = (dDomeCutOff*2) * 0.0049f;
    } else { // hopefully we can fix this in the next firmware and report proper values.
        domeVolts = domeVolts / 100.0;
        dDomeCutOff = dDomeCutOff / 100.0;
    }

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    ltime = time(NULL);
    timestamp = asctime(localtime(&ltime));
    timestamp[strlen(timestamp) - 1] = 0;
    fprintf(Logfile, "[%s] [CRTIDome::getBatteryLevels] domeVolts = %f\n", timestamp, domeVolts);
    fprintf(Logfile, "[%s] [CRTIDome::getBatteryLevels] dDomeCutOff = %f\n", timestamp, dDomeCutOff);
    fflush(Logfile);
#endif

    dShutterVolts  = 0;
    dShutterCutOff = 0;
    if(m_bShutterPresent) {
            //  Shutter
            
            nErr = domeCommand("K#", szResp, 'K', SERIAL_BUFFER_SIZE);
            if(nErr) {
        #if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
                ltime = time(NULL);
                timestamp = asctime(localtime(&ltime));
                timestamp[strlen(timestamp) - 1] = 0;
                fprintf(Logfile, "[%s] [CRTIDome::getBatteryLevels] ERROR = %s\n", timestamp, szResp);
                fflush(Logfile);
        #endif
                dShutterVolts = 0;
                dShutterCutOff = 0;
                return nErr;
            }

            if(strlen(szResp)<2) { // no shutter value
                dShutterVolts = -1;
                dShutterCutOff = -1;
                return nErr;
            }

            rc = sscanf(szResp, "%lf,%lf", &dShutterVolts, &dShutterCutOff);
            if(rc == 0) {
        #if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
                ltime = time(NULL);
                timestamp = asctime(localtime(&ltime));
                timestamp[strlen(timestamp) - 1] = 0;
                fprintf(Logfile, "[%s] [CRTIDome::getBatteryLevels] sscanf ERROR\n", timestamp);
                fflush(Logfile);
        #endif
                return COMMAND_FAILED;
            }
            // do a proper conversion as for some reason the value is scaled weirdly ( it's multiplied by 3/2)
            // Arduino ADC convert 0-5V to 0-1023 which is 0.0049V per unit
            if(m_fVersion < 2.0f) {
                dShutterVolts = (dShutterVolts*2) * 0.0049f;
                dShutterCutOff = (dShutterCutOff*2) * 0.0049f;
            } else { // hopefully we can fix this in the next firmware and report proper values.
                dShutterVolts = dShutterVolts / 100.0;
                dShutterCutOff = dShutterCutOff / 100.0;
            }
        #if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
            ltime = time(NULL);
            timestamp = asctime(localtime(&ltime));
            timestamp[strlen(timestamp) - 1] = 0;
            fprintf(Logfile, "[%s] [CRTIDome::getBatteryLevels] shutterVolts = %f\n", timestamp, dShutterVolts);
            fprintf(Logfile, "[%s] [CRTIDome::getBatteryLevels] dShutterCutOff = %f\n", timestamp, dShutterCutOff);
            fflush(Logfile);
        #endif
    }

    return nErr;
}

int CRTIDome::setBatteryCutOff(double dDomeCutOff, double dShutterCutOff)
{
    int nErr = ND_OK;
    char szBuf[SERIAL_BUFFER_SIZE];
    char szResp[SERIAL_BUFFER_SIZE];
    int nRotCutOff, nShutCutOff;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(m_bCalibrating)
        return nErr;

    if(m_fVersion < 2.0f) {
        nRotCutOff =  int(dDomeCutOff/0.0049f)/2;
        nShutCutOff =  int(dShutterCutOff/0.0049f)/2;
    }
    else {
        nRotCutOff = dDomeCutOff * 100.0;
        nShutCutOff = dShutterCutOff * 100.0;

    }

    // Dome
    snprintf(szBuf, SERIAL_BUFFER_SIZE, "k%d#", nRotCutOff);
    nErr = domeCommand(szBuf, szResp, 'k', SERIAL_BUFFER_SIZE);
    if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        ltime = time(NULL);
        timestamp = asctime(localtime(&ltime));
        timestamp[strlen(timestamp) - 1] = 0;
        fprintf(Logfile, "[%s] [CRTIDome::setBatteryCutOff] dDomeCutOff ERROR = %s\n", timestamp, szResp);
        fflush(Logfile);
#endif
        return nErr;
    }

    if(m_bShutterPresent) {
        // Shutter
        
        snprintf(szBuf, SERIAL_BUFFER_SIZE, "K%d#", nShutCutOff);
        nErr = domeCommand(szBuf, szResp, 'K', SERIAL_BUFFER_SIZE);
        if(nErr) {
    #if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
            ltime = time(NULL);
            timestamp = asctime(localtime(&ltime));
            timestamp[strlen(timestamp) - 1] = 0;
            fprintf(Logfile, "[%s] [CRTIDome::setBatteryCutOff] dShutterCutOff ERROR = %s\n", timestamp, szResp);
            fflush(Logfile);
    #endif
            return nErr;
        }
    }
    return nErr;
}

bool CRTIDome::isDomeMoving()
{
    bool bIsMoving;
    int nTmp;
    int nErr = ND_OK;
    char szResp[SERIAL_BUFFER_SIZE];

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    nErr = domeCommand("m#", szResp, 'm', SERIAL_BUFFER_SIZE);
    if(nErr & !m_bCalibrating) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        ltime = time(NULL);
        timestamp = asctime(localtime(&ltime));
        timestamp[strlen(timestamp) - 1] = 0;
        fprintf(Logfile, "[%s] [CRTIDome::isDomeMoving] ERROR = %s\n", timestamp, szResp);
        fflush(Logfile);
#endif
        return false;
    }
    else if (nErr & m_bCalibrating) {
        return true;
    }

    bIsMoving = false;
    nTmp = atoi(szResp);
#ifdef PLUGIN_DEBUG
    ltime = time(NULL);
    timestamp = asctime(localtime(&ltime));
    timestamp[strlen(timestamp) - 1] = 0;
    fprintf(Logfile, "[%s] CRTIDome::isDomeMoving nTmp : %d\n", timestamp, nTmp);
    fflush(Logfile);
#endif
    if(nTmp)
        bIsMoving = true;

#ifdef PLUGIN_DEBUG
    ltime = time(NULL);
    timestamp = asctime(localtime(&ltime));
    timestamp[strlen(timestamp) - 1] = 0;
    fprintf(Logfile, "[%s] CRTIDome::isDomeMoving bIsMoving : %s\n", timestamp, bIsMoving?"True":"False");
    fflush(Logfile);
#endif

    return bIsMoving;
}

bool CRTIDome::isDomeAtHome()
{
    bool bAthome;
    int nTmp;
    int nErr = ND_OK;
    char szResp[SERIAL_BUFFER_SIZE];

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    nErr = domeCommand("z#", szResp, 'z', SERIAL_BUFFER_SIZE);
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    ltime = time(NULL);
    timestamp = asctime(localtime(&ltime));
    timestamp[strlen(timestamp) - 1] = 0;
    fprintf(Logfile, "[%s] [CRTIDome::isDomeAtHome] z# response = %s\n", timestamp, szResp);
    fflush(Logfile);
#endif
    if(nErr) {
        return false;
    }

    bAthome = false;
    nTmp = atoi(szResp);
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    ltime = time(NULL);
    timestamp = asctime(localtime(&ltime));
    timestamp[strlen(timestamp) - 1] = 0;
    fprintf(Logfile, "[%s] [CRTIDome::isDomeAtHome] nTmp = %d\n", timestamp, nTmp);
    fflush(Logfile);
#endif
    if(nTmp == ATHOME)
        bAthome = true;
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    ltime = time(NULL);
    timestamp = asctime(localtime(&ltime));
    timestamp[strlen(timestamp) - 1] = 0;
    fprintf(Logfile, "[%s] CRTIDome::isDomeAtHome bAthome : %s\n", timestamp, bAthome?"True":"False");
    fflush(Logfile);
#endif

    return bAthome;

}

int CRTIDome::syncDome(double dAz, double dEl)
{
    int nErr = ND_OK;
    char szBuf[SERIAL_BUFFER_SIZE];
    char szResp[SERIAL_BUFFER_SIZE];

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    m_dCurrentAzPosition = dAz;
    snprintf(szBuf, SERIAL_BUFFER_SIZE, "s%3.2f#", dAz);
    nErr = domeCommand(szBuf, szResp, 's', SERIAL_BUFFER_SIZE);
    if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        ltime = time(NULL);
        timestamp = asctime(localtime(&ltime));
        timestamp[strlen(timestamp) - 1] = 0;
        fprintf(Logfile, "[%s] [CRTIDome::syncDome] ERROR = %d\n", timestamp, nErr);
        fflush(Logfile);
#endif
        return nErr;
    }
    // TODO : Also set Elevation when supported by the firmware.
    // m_dCurrentElPosition = dEl;
    return nErr;
}

int CRTIDome::parkDome()
{
    int nErr = ND_OK;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(m_bHomeOnPark) {
        m_bParking = true;
        nErr = goHome();
    } else
        nErr = gotoAzimuth(m_dParkAz);

    return nErr;

}

int CRTIDome::unparkDome()
{
    if(m_bHomeOnUnpark) {
        m_bUnParking = true;
        goHome();
    }
    else {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        ltime = time(NULL);
        timestamp = asctime(localtime(&ltime));
        timestamp[strlen(timestamp) - 1] = 0;
        fprintf(Logfile, "[%s] [CRTIDome::unparkDome] m_dParkAz = %3.3f\n", timestamp, m_dParkAz);
        fflush(Logfile);
#endif
        syncDome(m_dParkAz, m_dCurrentElPosition);
        m_bParked = false;
        m_bUnParking = false;
    }

    return 0;
}

int CRTIDome::gotoAzimuth(double dNewAz)
{
    int nErr = ND_OK;
    char szBuf[SERIAL_BUFFER_SIZE];
    char szResp[SERIAL_BUFFER_SIZE];

    if(!m_bIsConnected)
        return NOT_CONNECTED;


    while(dNewAz >= 360)
        dNewAz = dNewAz - 360;

    snprintf(szBuf, SERIAL_BUFFER_SIZE, "g%3.2f#", dNewAz);
    nErr = domeCommand(szBuf, szResp, 'g', SERIAL_BUFFER_SIZE);
    if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        ltime = time(NULL);
        timestamp = asctime(localtime(&ltime));
        timestamp[strlen(timestamp) - 1] = 0;
        fprintf(Logfile, "[%s] [CRTIDome::gotoAzimuth] ERROR = %d\n", timestamp, nErr);
        fflush(Logfile);
#endif
        return nErr;
    }

    m_dGotoAz = dNewAz;
    m_nGotoTries = 0;
    return nErr;
}

int CRTIDome::openShutter()
{
    int nErr = ND_OK;
    bool bDummy;
    char szResp[SERIAL_BUFFER_SIZE];
    double domeVolts;
    double dDomeCutOff;
    double dShutterVolts;
    double dShutterCutOff;
    
    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(m_bCalibrating)
        return SB_OK;

    getShutterPresent(bDummy);
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    ltime = time(NULL);
    timestamp = asctime(localtime(&ltime));
    timestamp[strlen(timestamp) - 1] = 0;
    fprintf(Logfile, "[%s] [CRTIDome::openShutter] m_bShutterPresent = %s\n", timestamp, m_bShutterPresent?"Yes":"No");
    fflush(Logfile);
#endif
    if(!m_bShutterPresent) {
        return SB_OK;
    }

    getBatteryLevels(domeVolts, dDomeCutOff, dShutterVolts, dShutterCutOff);
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    ltime = time(NULL);
    timestamp = asctime(localtime(&ltime));
    timestamp[strlen(timestamp) - 1] = 0;
    fprintf(Logfile, "[%s] [CRTIDome::openShutter] Opening shutter\n", timestamp);
    fflush(Logfile);
#endif

	
    nErr = domeCommand("O#", szResp, 'O', SERIAL_BUFFER_SIZE);
    if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        ltime = time(NULL);
        timestamp = asctime(localtime(&ltime));
        timestamp[strlen(timestamp) - 1] = 0;
        fprintf(Logfile, "[%s] [CRTIDome::openShutter] ERROR = %d\n", timestamp, nErr);
        fflush(Logfile);
#endif
    }
    if(szResp[0] == 'L') { // batteryb LOW.. can't open
        nErr = ERR_CMDFAILED;
    }
    return nErr;
}

int CRTIDome::closeShutter()
{
    int nErr = ND_OK;
    bool bDummy;
    char szResp[SERIAL_BUFFER_SIZE];
    double domeVolts;
    double dDomeCutOff;
    double dShutterVolts;
    double dShutterCutOff;
    
    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(m_bCalibrating)
        return SB_OK;

    getShutterPresent(bDummy);
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    ltime = time(NULL);
    timestamp = asctime(localtime(&ltime));
    timestamp[strlen(timestamp) - 1] = 0;
    fprintf(Logfile, "[%s] [CRTIDome::closeShutter] m_bShutterPresent = %s\n", timestamp, m_bShutterPresent?"Yes":"No");
    fflush(Logfile);
#endif

    if(!m_bShutterPresent) {
        return SB_OK;
    }

    getBatteryLevels(domeVolts, dDomeCutOff, dShutterVolts, dShutterCutOff);

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    ltime = time(NULL);
    timestamp = asctime(localtime(&ltime));
    timestamp[strlen(timestamp) - 1] = 0;
    fprintf(Logfile, "[%s] [CRTIDome::closeShutter] Closing shutter\n", timestamp);
    fflush(Logfile);
#endif

	
    nErr = domeCommand("C#", szResp, 'C', SERIAL_BUFFER_SIZE);
    if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        ltime = time(NULL);
        timestamp = asctime(localtime(&ltime));
        timestamp[strlen(timestamp) - 1] = 0;
        fprintf(Logfile, "[%s] [CRTIDome::openShutter] closeShutter = %d\n", timestamp, nErr);
        fflush(Logfile);
#endif
    }

    if(szResp[0] == 'L') { // batteryb LOW.. can't open
        nErr = ERR_CMDFAILED;
    }

    return nErr;
}

int CRTIDome::getFirmwareVersion(char *szVersion, int nStrMaxLen)
{
    int nErr = ND_OK;
    int i;
    char szResp[SERIAL_BUFFER_SIZE];
    std::vector<std::string> firmwareFields;
    std::vector<std::string> versionFields;
    std::string strVersion;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(m_bCalibrating)
        return SB_OK;

    nErr = domeCommand("v#", szResp, 'v', SERIAL_BUFFER_SIZE);
    if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        ltime = time(NULL);
        timestamp = asctime(localtime(&ltime));
        timestamp[strlen(timestamp) - 1] = 0;
        fprintf(Logfile, "[%s] [CRTIDome::getFirmwareVersion] ERROR = %s\n", timestamp, szResp);
        fflush(Logfile);
#endif
        return nErr;
    }

    nErr = parseFields(szResp,firmwareFields, 'v');
    if(nErr) {
        strncpy(szVersion, szResp, nStrMaxLen);
        m_fVersion = atof(szResp);
        return ND_OK;
    }

    nErr = parseFields(firmwareFields[0].c_str(),versionFields, '.');
    if(versionFields.size()>1) {
        strVersion=versionFields[0]+".";
        for(i=1; i<versionFields.size(); i++) {
            strVersion+=versionFields[i];
        }
        strncpy(szVersion, szResp, nStrMaxLen);
        m_fVersion = atof(strVersion.c_str());
    }
    else {
        strncpy(szVersion, szResp, nStrMaxLen);
        m_fVersion = atof(szResp);
    }
    return nErr;
}

int CRTIDome::getFirmwareVersion(float &fVersion)
{
    int nErr = ND_OK;

    if(m_fVersion == 0.0f) {
        nErr = getFirmwareVersion(m_szFirmwareVersion, SERIAL_BUFFER_SIZE);
        if(nErr)
            return nErr;
    }

    fVersion = m_fVersion;

    return nErr;
}

int CRTIDome::getShutterFirmwareVersion(char *szVersion, int nStrMaxLen)
{
    int nErr = ND_OK;
    char szResp[SERIAL_BUFFER_SIZE];
    std::vector<std::string> firmwareFields;
    std::vector<std::string> versionFields;
    std::string strVersion;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(m_bCalibrating)
        return SB_OK;

    nErr = domeCommand("V#", szResp, 'V', SERIAL_BUFFER_SIZE);
    if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        ltime = time(NULL);
        timestamp = asctime(localtime(&ltime));
        timestamp[strlen(timestamp) - 1] = 0;
        fprintf(Logfile, "[%s] [CRTIDome::getShutterFirmwareVersion] ERROR = %s\n", timestamp, szResp);
        fflush(Logfile);
#endif
        return nErr;
    }

    nErr = parseFields(szResp,firmwareFields, 'V');
    if(nErr) {
        strncpy(szVersion, szResp, nStrMaxLen);
        m_fVersion = atof(szResp);
        return ND_OK;
    }

    strncpy(szVersion, szResp, nStrMaxLen);
    m_fVersion = atof(szResp);
    return nErr;
}

int CRTIDome::goHome()
{
    int nErr = ND_OK;
    char szResp[SERIAL_BUFFER_SIZE];

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(m_bCalibrating) {
        return SB_OK;
    }
    else if(isDomeAtHome()){
            m_bHomed = true;
            return ND_OK;
    }
#ifdef PLUGIN_DEBUG
    ltime = time(NULL);
    timestamp = asctime(localtime(&ltime));
    timestamp[strlen(timestamp) - 1] = 0;
    fprintf(Logfile, "[%s] CRTIDome::goHome \n", timestamp);
    fflush(Logfile);
#endif

    m_nHomingTries = 0;
    nErr = domeCommand("h#", szResp, 'h', SERIAL_BUFFER_SIZE);
    if(nErr) {
#ifdef PLUGIN_DEBUG
        ltime = time(NULL);
        timestamp = asctime(localtime(&ltime));
        timestamp[strlen(timestamp) - 1] = 0;
        fprintf(Logfile, "[%s] CRTIDome::goHome ERROR = %d\n", timestamp, nErr);
        fflush(Logfile);
#endif
        return nErr;
    }

    return nErr;
}

int CRTIDome::calibrate()
{
    int nErr = ND_OK;
    char szResp[SERIAL_BUFFER_SIZE];

    if(!m_bIsConnected)
        return NOT_CONNECTED;


    nErr = domeCommand("c#", szResp, 'c', SERIAL_BUFFER_SIZE);
    if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        ltime = time(NULL);
        timestamp = asctime(localtime(&ltime));
        timestamp[strlen(timestamp) - 1] = 0;
        fprintf(Logfile, "[%s] [CRTIDome::calibrate] ERROR = %d\n", timestamp, nErr);
        fflush(Logfile);
#endif
        return nErr;
    }
    m_bCalibrating = true;

    return nErr;
}

int CRTIDome::isGoToComplete(bool &bComplete)
{
    int nErr = ND_OK;
    double dDomeAz = 0;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(isDomeMoving()) {
        bComplete = false;
        getDomeAz(dDomeAz);
        return nErr;
    }

    getDomeAz(dDomeAz);
    if(dDomeAz >0 && dDomeAz<1)
        dDomeAz = 0;

    while(ceil(m_dGotoAz) >= 360)
          m_dGotoAz = ceil(m_dGotoAz) - 360;

    while(ceil(dDomeAz) >= 360)
        dDomeAz = ceil(dDomeAz) - 360;

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    ltime = time(NULL);
    timestamp = asctime(localtime(&ltime));
    timestamp[strlen(timestamp) - 1] = 0;
    fprintf(Logfile, "[%s] CRTIDome::isGoToComplete DomeAz = %3.2f\n", timestamp, dDomeAz);
    fflush(Logfile);
#endif

    // we need to test "large" depending on the heading error , this is new in firmware 1.10 and up
    if ((ceil(m_dGotoAz) <= ceil(dDomeAz)+3) && (ceil(m_dGotoAz) >= ceil(dDomeAz)-3)) {
        bComplete = true;
        m_nGotoTries = 0;
    }
    else {
        // we're not moving and we're not at the final destination !!!
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        ltime = time(NULL);
        timestamp = asctime(localtime(&ltime));
        timestamp[strlen(timestamp) - 1] = 0;
        fprintf(Logfile, "[%s] CRTIDome::isGoToComplete ***** ERROR **** domeAz = %3.2f, m_dGotoAz = %3.2f\n", timestamp, dDomeAz, m_dGotoAz);
        fflush(Logfile);
#endif
        if(m_nGotoTries == 0) {
            bComplete = false;
            m_nGotoTries = 1;
            gotoAzimuth(m_dGotoAz);
        }
        else {
            m_nGotoTries = 0;
            nErr = ERR_CMDFAILED;
        }
    }

    return nErr;
}

int CRTIDome::isOpenComplete(bool &bComplete)
{
    int nErr = ND_OK;
    int nState;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(!m_bShutterPresent) {
        bComplete = true;
        return SB_OK;
    }

    nErr = getShutterState(nState);
    if(nErr)
        return ERR_CMDFAILED;
    if(nState == OPEN){
        m_bShutterOpened = true;
        bComplete = true;
        m_dCurrentElPosition = 90.0;
    }
    else {
        m_bShutterOpened = false;
        bComplete = false;
        m_dCurrentElPosition = 0.0;
    }

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    ltime = time(NULL);
    timestamp = asctime(localtime(&ltime));
    timestamp[strlen(timestamp) - 1] = 0;
    fprintf(Logfile, "[%s] CRTIDome::isOpenComplete bComplete = %s\n", timestamp, bComplete?"True":"False");
    fflush(Logfile);
#endif

    return nErr;
}

int CRTIDome::isCloseComplete(bool &bComplete)
{
    int nErr = ND_OK;
    int nState;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(!m_bShutterPresent) {
        bComplete = true;
        return SB_OK;
    }

    nErr = getShutterState(nState);
    if(nErr)
        return ERR_CMDFAILED;
    if(nState == CLOSED){
        m_bShutterOpened = false;
        bComplete = true;
        m_dCurrentElPosition = 0.0;
    }
    else {
        m_bShutterOpened = true;
        bComplete = false;
        m_dCurrentElPosition = 90.0;
    }

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    ltime = time(NULL);
    timestamp = asctime(localtime(&ltime));
    timestamp[strlen(timestamp) - 1] = 0;
    fprintf(Logfile, "[%s] CRTIDome::isCloseComplete bComplete = %s\n", timestamp, bComplete?"True":"False");
    fflush(Logfile);
#endif

    return nErr;
}


int CRTIDome::isParkComplete(bool &bComplete)
{
    int nErr = ND_OK;
    double dDomeAz=0;
    bool bFoundHome;

    if(!m_bIsConnected)
        return NOT_CONNECTED;
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    ltime = time(NULL);
    timestamp = asctime(localtime(&ltime));
    timestamp[strlen(timestamp) - 1] = 0;
    fprintf(Logfile, "[%s] CRTIDome::isParkComplete m_bParking = %s\n", timestamp, m_bParking?"True":"False");
    fprintf(Logfile, "[%s] CRTIDome::isParkComplete bComplete = %s\n", timestamp, bComplete?"True":"False");
    fflush(Logfile);
#endif

    if(isDomeMoving()) {
        getDomeAz(dDomeAz);
        bComplete = false;
        return nErr;
    }

    if(m_bParking) {
        bComplete = false;
        nErr = isFindHomeComplete(bFoundHome);
        if(bFoundHome) { // we're home, now park
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
            ltime = time(NULL);
            timestamp = asctime(localtime(&ltime));
            timestamp[strlen(timestamp) - 1] = 0;
            fprintf(Logfile, "[%s] CRTIDome::isParkComplete found home, now parking\n", timestamp);
            fflush(Logfile);
#endif
            m_bParking = false;
            nErr = gotoAzimuth(m_dParkAz);
        }
        return nErr;
    }

    getDomeAz(dDomeAz);

    // we need to test "large" depending on the heading error
    if ((ceil(m_dParkAz) <= ceil(dDomeAz)+3) && (ceil(m_dParkAz) >= ceil(dDomeAz)-3)) {
        m_bParked = true;
        bComplete = true;
    }
    else {
        // we're not moving and we're not at the final destination !!!
        bComplete = false;
        m_bHomed = false;
        m_bParked = false;
        nErr = ERR_CMDFAILED;
    }

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    ltime = time(NULL);
    timestamp = asctime(localtime(&ltime));
    timestamp[strlen(timestamp) - 1] = 0;
    fprintf(Logfile, "[%s] CRTIDome::isParkComplete bComplete = %s\n", timestamp, bComplete?"True":"False");
    fflush(Logfile);
#endif

    return nErr;
}

int CRTIDome::isUnparkComplete(bool &bComplete)
{
    int nErr = ND_OK;

    bComplete = false;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(!m_bParked) {
        bComplete = true;
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        ltime = time(NULL);
        timestamp = asctime(localtime(&ltime));
        timestamp[strlen(timestamp) - 1] = 0;
        fprintf(Logfile, "[%s] CRTIDome::isUnparkComplete UNPARKED \n", timestamp);
        fflush(Logfile);
#endif
    }
    else if (m_bUnParking) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        ltime = time(NULL);
        timestamp = asctime(localtime(&ltime));
        timestamp[strlen(timestamp) - 1] = 0;
        fprintf(Logfile, "[%s] CRTIDome::isUnparkComplete unparking.. checking if we're home \n", timestamp);
        fflush(Logfile);
#endif
        nErr = isFindHomeComplete(bComplete);
        if(nErr)
            return nErr;
        if(bComplete) {
            m_bParked = false;
        }
        else {
            m_bParked = true;
        }
    }

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    ltime = time(NULL);
    timestamp = asctime(localtime(&ltime));
    timestamp[strlen(timestamp) - 1] = 0;
    fprintf(Logfile, "[%s] CRTIDome::isUnparkComplete m_bParked = %s\n", timestamp, m_bParked?"True":"False");
    fprintf(Logfile, "[%s] CRTIDome::isUnparkComplete bComplete = %s\n", timestamp, bComplete?"True":"False");
    fflush(Logfile);
#endif

    return nErr;
}

int CRTIDome::isFindHomeComplete(bool &bComplete)
{
    int nErr = ND_OK;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

#ifdef PLUGIN_DEBUG
    ltime = time(NULL);
    timestamp = asctime(localtime(&ltime));
    timestamp[strlen(timestamp) - 1] = 0;
    fprintf(Logfile, "[%s] CRTIDome::isFindHomeComplete\n", timestamp);
    fflush(Logfile);
#endif

    if(isDomeMoving()) {
        m_bHomed = false;
        bComplete = false;
#ifdef PLUGIN_DEBUG
        ltime = time(NULL);
        timestamp = asctime(localtime(&ltime));
        timestamp[strlen(timestamp) - 1] = 0;
        fprintf(Logfile, "[%s] CRTIDome::isFindHomeComplete still moving\n", timestamp);
        fflush(Logfile);
#endif
        return nErr;

    }

    if(isDomeAtHome()){
        m_bHomed = true;
        bComplete = true;
        if(m_bUnParking)
            m_bParked = false;
        syncDome(m_dHomeAz, m_dCurrentElPosition);
        m_nHomingTries = 0;
#ifdef PLUGIN_DEBUG
        ltime = time(NULL);
        timestamp = asctime(localtime(&ltime));
        timestamp[strlen(timestamp) - 1] = 0;
        fprintf(Logfile, "[%s] CRTIDome::isFindHomeComplete At Home\n", timestamp);
        fflush(Logfile);
#endif
    }
    else {
        // we're not moving and we're not at the home position !!!
#ifdef PLUGIN_DEBUG
        ltime = time(NULL);
        timestamp = asctime(localtime(&ltime));
        timestamp[strlen(timestamp) - 1] = 0;
        fprintf(Logfile, "[%s] [CRTIDome::isFindHomeComplete] Not moving and not at home !!!\n", timestamp);
        fflush(Logfile);
#endif
        bComplete = false;
        m_bHomed = false;
        m_bParked = false;
        // sometimes we pass the home sensor and the dome doesn't rotate back enough to detect it.
        // this is mostly the case with firmware 1.10 with the new error correction ...
        // so give it another try
        if(m_nHomingTries == 0) {
            m_nHomingTries = 1;
            goHome();
        }
        return ERR_CMDFAILED;
    }

    return nErr;
}


int CRTIDome::isCalibratingComplete(bool &bComplete)
{
    int nErr = ND_OK;
    double dDomeAz = 0;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(isDomeMoving()) {
        // getDomeAz(dDomeAz);
        m_bHomed = false;
        bComplete = false;
        return nErr;
    }


    nErr = getDomeAz(dDomeAz);

    if (ceil(m_dHomeAz) != ceil(dDomeAz)) {
        // We need to resync the current position to the home position.
        m_dCurrentAzPosition = m_dHomeAz;
        syncDome(m_dCurrentAzPosition,m_dCurrentElPosition);
        m_bHomed = true;
        bComplete = true;
    }

    nErr = getDomeStepPerRev(m_nNbStepPerRev);
    m_bHomed = true;
    bComplete = true;
    m_bCalibrating = false;
#ifdef PLUGIN_DEBUG
    ltime = time(NULL);
    timestamp = asctime(localtime(&ltime));
    timestamp[strlen(timestamp) - 1] = 0;
    fprintf(Logfile, "[%s] [CRTIDome::getNbTicksPerRev] final m_nNbStepPerRev = %d\n", timestamp, m_nNbStepPerRev);
    fprintf(Logfile, "[%s] [CRTIDome::getNbTicksPerRev] final m_bHomed = %s\n", timestamp, m_bHomed?"True":"False");
    fprintf(Logfile, "[%s] [CRTIDome::getNbTicksPerRev] final m_bCalibrating = %s\n", timestamp, m_bCalibrating?"True":"False");
    fprintf(Logfile, "[%s] [CRTIDome::getNbTicksPerRev] final bComplete = %s\n", timestamp, bComplete?"True":"False");
    fflush(Logfile);
#endif
    return nErr;
}


int CRTIDome::abortCurrentCommand()
{
    int nErr = ND_OK;
    char szResp[SERIAL_BUFFER_SIZE];

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    m_bHomed = false;
    m_bParked = false;
    m_bCalibrating = false;
    m_bParking = false;
    m_bUnParking = false;
    m_nGotoTries = 1;   // prevents the goto retry
    m_nHomingTries = 1; // prevents the find home retry

    nErr = domeCommand("a#", szResp, 'a', SERIAL_BUFFER_SIZE);

    getDomeAz(m_dGotoAz);

    return nErr;
}

int CRTIDome::sendShutterHello()
{
    int nErr = ND_OK;
    char szResp[SERIAL_BUFFER_SIZE];

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(!m_bShutterPresent) {
        return SB_OK;
    }

	
	if(m_fVersion>=2.0f)
        nErr = domeCommand("H#", szResp, 'H', SERIAL_BUFFER_SIZE);
    else
        nErr = domeCommand("H#", NULL, 0, SERIAL_BUFFER_SIZE);
    return nErr;
}

int CRTIDome::getShutterPresent(bool &bShutterPresent)
{
    int nErr = ND_OK;
    char szResp[SERIAL_BUFFER_SIZE];

    nErr = domeCommand("o#", szResp, 'o', SERIAL_BUFFER_SIZE);
    if(nErr) {
        return nErr;
    }

    m_bShutterPresent = (szResp[0]=='1') ? true : false;
#ifdef PLUGIN_DEBUG
    ltime = time(NULL);
    timestamp = asctime(localtime(&ltime));
    timestamp[strlen(timestamp) - 1] = 0;
    fprintf(Logfile, "[%s] [CRTIDome::getShutterPresent] szResp =  %s\n", timestamp, szResp);
    fprintf(Logfile, "[%s] [CRTIDome::getShutterPresent] m_bShutterPresent =  %s\n", timestamp, m_bShutterPresent?"Yes":"No");
    fflush(Logfile);
#endif


    bShutterPresent = m_bShutterPresent;
    return nErr;

}

#pragma mark - Getter / Setter

int CRTIDome::getNbTicksPerRev()
{
#ifdef PLUGIN_DEBUG
    ltime = time(NULL);
    timestamp = asctime(localtime(&ltime));
    timestamp[strlen(timestamp) - 1] = 0;
    fprintf(Logfile, "[%s] [CRTIDome::getNbTicksPerRev] m_bIsConnected = %s\n", timestamp, m_bIsConnected?"True":"False");
    fflush(Logfile);
#endif

    if(m_bIsConnected)
        getDomeStepPerRev(m_nNbStepPerRev);

#ifdef PLUGIN_DEBUG
    ltime = time(NULL);
    timestamp = asctime(localtime(&ltime));
    timestamp[strlen(timestamp) - 1] = 0;
    fprintf(Logfile, "[%s] [CRTIDome::getNbTicksPerRev] m_nNbStepPerRev = %d\n", timestamp, m_nNbStepPerRev);
    fflush(Logfile);
#endif

    return m_nNbStepPerRev;
}

int CRTIDome::setNbTicksPerRev(int nSteps)
{
    int nErr = ND_OK;

    if(m_bIsConnected)
        nErr = setDomeStepPerRev(nSteps);
    return nErr;
}

double CRTIDome::getHomeAz()
{
    if(m_bIsConnected)
        getDomeHomeAz(m_dHomeAz);
    return m_dHomeAz;
}

int CRTIDome::setHomeAz(double dAz)
{
    int nErr = ND_OK;
    char szBuf[SERIAL_BUFFER_SIZE];
    char szResp[SERIAL_BUFFER_SIZE];

    m_dHomeAz = dAz;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    snprintf(szBuf, SERIAL_BUFFER_SIZE, "i%3.2f#", dAz);
    nErr = domeCommand(szBuf, szResp, 'i', SERIAL_BUFFER_SIZE);
    return nErr;
}


double CRTIDome::getParkAz()
{
    if(m_bIsConnected)
        getDomeParkAz(m_dParkAz);

    return m_dParkAz;

}

int CRTIDome::setParkAz(double dAz)
{
    int nErr = ND_OK;
    char szBuf[SERIAL_BUFFER_SIZE];
    char szResp[SERIAL_BUFFER_SIZE];

    m_dParkAz = dAz;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    snprintf(szBuf, SERIAL_BUFFER_SIZE, "l%3.2f#", dAz);
    nErr = domeCommand(szBuf, szResp, 'l', SERIAL_BUFFER_SIZE);
    return nErr;
}


double CRTIDome::getCurrentAz()
{

    if(m_bIsConnected) {
        getDomeAz(m_dCurrentAzPosition);
   }
    return m_dCurrentAzPosition;
}

double CRTIDome::getCurrentEl()
{
    if(m_bIsConnected)
        getDomeEl(m_dCurrentElPosition);

    return m_dCurrentElPosition;
}

int CRTIDome::getCurrentShutterState()
{
    if(m_bIsConnected)
        getShutterState(m_nShutterState);

    return m_nShutterState;
}


int CRTIDome::getDefaultDir(bool &bNormal)
{
    int nErr = ND_OK;
    char szResp[SERIAL_BUFFER_SIZE];

    bNormal = true;
    nErr = domeCommand("y#", szResp, 'y', SERIAL_BUFFER_SIZE);
    if(nErr) {
        return nErr;
    }

    bNormal = atoi(szResp) ? false:true;
#ifdef PLUGIN_DEBUG
    ltime = time(NULL);
    timestamp = asctime(localtime(&ltime));
    timestamp[strlen(timestamp) - 1] = 0;
    fprintf(Logfile, "[%s] [CRTIDome::getDefaultDir] bNormal =  %s\n", timestamp, bNormal?"True":"False");
    fflush(Logfile);
#endif


    return nErr;
}

int CRTIDome::setDefaultDir(bool bNormal)
{
    int nErr = ND_OK;
    char szBuf[SERIAL_BUFFER_SIZE];
    char szResp[SERIAL_BUFFER_SIZE];

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    snprintf(szBuf, SERIAL_BUFFER_SIZE, "y %1d#", bNormal?0:1);

#ifdef PLUGIN_DEBUG
    ltime = time(NULL);
    timestamp = asctime(localtime(&ltime));
    timestamp[strlen(timestamp) - 1] = 0;
    fprintf(Logfile, "[%s] [CRTIDome::setDefaultDir] bNormal =  %s\n", timestamp, bNormal?"True":"False");
    fprintf(Logfile, "[%s] [CRTIDome::setDefaultDir] szBuf =  %s\n", timestamp, szBuf);
    fflush(Logfile);
#endif

    nErr = domeCommand(szBuf, szResp, 'y', SERIAL_BUFFER_SIZE);
    return nErr;

}

int CRTIDome::getRainSensorStatus(int &nStatus)
{
    int nErr = ND_OK;
    char szResp[SERIAL_BUFFER_SIZE];

    nStatus = NOT_RAINING;
    nErr = domeCommand("F#", szResp, 'F', SERIAL_BUFFER_SIZE);
    if(nErr) {
        return nErr;
    }

    nStatus = atoi(szResp) ? false:true;
#ifdef PLUGIN_DEBUG
    ltime = time(NULL);
    timestamp = asctime(localtime(&ltime));
    timestamp[strlen(timestamp) - 1] = 0;
    fprintf(Logfile, "[%s] [CRTIDome::getRainSensorStatus] nStatus =  %s\n", timestamp, nStatus?"NOT RAINING":"RAINING");
    fflush(Logfile);
#endif


    m_nIsRaining = nStatus;
    return nErr;
}

int CRTIDome::getRotationSpeed(int &nSpeed)
{
    int nErr = ND_OK;
    char szResp[SERIAL_BUFFER_SIZE];

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    nErr = domeCommand("r#", szResp, 'r', SERIAL_BUFFER_SIZE);
    if(nErr) {
        return nErr;
    }

    nSpeed = atoi(szResp);
#ifdef PLUGIN_DEBUG
    ltime = time(NULL);
    timestamp = asctime(localtime(&ltime));
    timestamp[strlen(timestamp) - 1] = 0;
    fprintf(Logfile, "[%s] [CRTIDome::getRotationSpeed] nSpeed =  %d\n", timestamp, nSpeed);
    fflush(Logfile);
#endif

    return nErr;
}

int CRTIDome::setRotationSpeed(int nSpeed)
{
    int nErr = ND_OK;
    char szBuf[SERIAL_BUFFER_SIZE];
    char szResp[SERIAL_BUFFER_SIZE];

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    snprintf(szBuf, SERIAL_BUFFER_SIZE, "r%d#", nSpeed);
    nErr = domeCommand(szBuf, szResp, 'r', SERIAL_BUFFER_SIZE);
    return nErr;
}


int CRTIDome::getRotationAcceleration(int &nAcceleration)
{
    int nErr = ND_OK;
    char szResp[SERIAL_BUFFER_SIZE];

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    nErr = domeCommand("e#", szResp, 'e', SERIAL_BUFFER_SIZE);
    if(nErr) {
        return nErr;
    }

    nAcceleration = atoi(szResp);
#ifdef PLUGIN_DEBUG
    ltime = time(NULL);
    timestamp = asctime(localtime(&ltime));
    timestamp[strlen(timestamp) - 1] = 0;
    fprintf(Logfile, "[%s] [CRTIDome::getRotationAcceleration] nAcceleration =  %d\n", timestamp, nAcceleration);
    fflush(Logfile);
#endif

    return nErr;
}

int CRTIDome::setRotationAcceleration(int nAcceleration)
{
    int nErr = ND_OK;
    char szBuf[SERIAL_BUFFER_SIZE];
    char szResp[SERIAL_BUFFER_SIZE];

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    snprintf(szBuf, SERIAL_BUFFER_SIZE, "e%d#", nAcceleration);
    nErr = domeCommand(szBuf, szResp, 'e', SERIAL_BUFFER_SIZE);

    return nErr;
}

int CRTIDome::getShutterSpeed(int &nSpeed)
{
    int nErr = ND_OK;
    char szResp[SERIAL_BUFFER_SIZE];

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(!m_bShutterPresent) {
        nSpeed = 0;
        return SB_OK;
    }

	
    nErr = domeCommand("R#", szResp, 'R', SERIAL_BUFFER_SIZE);
    if(nErr) {
        return nErr;
    }

    nSpeed = atoi(szResp);
#ifdef PLUGIN_DEBUG
    ltime = time(NULL);
    timestamp = asctime(localtime(&ltime));
    timestamp[strlen(timestamp) - 1] = 0;
    fprintf(Logfile, "[%s] [CRTIDome::getShutterSpeed] nSpeed =  %d\n", timestamp, nSpeed);
    fflush(Logfile);
#endif

    return nErr;
}

int CRTIDome::setShutterSpeed(int nSpeed)
{
    int nErr = ND_OK;
    char szBuf[SERIAL_BUFFER_SIZE];
    char szResp[SERIAL_BUFFER_SIZE];

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(!m_bShutterPresent) {
        return SB_OK;
    }

	
    snprintf(szBuf, SERIAL_BUFFER_SIZE, "R%d#", nSpeed);
    nErr = domeCommand(szBuf, szResp, 'R', SERIAL_BUFFER_SIZE);

    return nErr;
}

int CRTIDome::getShutterAcceleration(int &nAcceleration)
{
    int nErr = ND_OK;
    char szResp[SERIAL_BUFFER_SIZE];

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(!m_bShutterPresent) {
        nAcceleration = 0;
        return SB_OK;
    }

	
    nErr = domeCommand("E#", szResp, 'E', SERIAL_BUFFER_SIZE);
    if(nErr) {
        return nErr;
    }

    nAcceleration = atoi(szResp);
#ifdef PLUGIN_DEBUG
    ltime = time(NULL);
    timestamp = asctime(localtime(&ltime));
    timestamp[strlen(timestamp) - 1] = 0;
    fprintf(Logfile, "[%s] [CRTIDome::getShutterAcceleration] nAcceleration =  %d\n", timestamp, nAcceleration);
    fflush(Logfile);
#endif
    return nErr;
}

int CRTIDome::setShutterAcceleration(int nAcceleration)
{
    int nErr = ND_OK;
    char szBuf[SERIAL_BUFFER_SIZE];
    char szResp[SERIAL_BUFFER_SIZE];

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(!m_bShutterPresent) {
        return SB_OK;
    }

	
    snprintf(szBuf, SERIAL_BUFFER_SIZE, "E%d#", nAcceleration);
    nErr = domeCommand(szBuf, szResp, 'E', SERIAL_BUFFER_SIZE);
    return nErr;
}

void CRTIDome::setHomeOnPark(const bool bEnabled)
{
    m_bHomeOnPark = bEnabled;
}

void CRTIDome::setHomeOnUnpark(const bool bEnabled)
{
    m_bHomeOnUnpark = bEnabled;
}

int	CRTIDome::getSutterWatchdogTimerValue(int &nValue)
{
	int nErr = ND_OK;
	char szResp[SERIAL_BUFFER_SIZE];

	if(!m_bIsConnected)
		return NOT_CONNECTED;

    if(!m_bShutterPresent) {
        nValue = 0;
        return SB_OK;
    }

	
	nErr = domeCommand("I#", szResp, 'I', SERIAL_BUFFER_SIZE);
	if(nErr) {
		return nErr;
	}

	nValue = atoi(szResp)/1000; // value is in ms
#ifdef PLUGIN_DEBUG
	ltime = time(NULL);
	timestamp = asctime(localtime(&ltime));
	timestamp[strlen(timestamp) - 1] = 0;
	fprintf(Logfile, "[%s] [CRTIDome::getSutterWatchdogTimerValue] nValue =  %d\n", timestamp, nValue);
	fflush(Logfile);
#endif
	return nErr;
}

int	CRTIDome::setSutterWatchdogTimerValue(const int &nValue)
{
	int nErr = ND_OK;
	char szBuf[SERIAL_BUFFER_SIZE];
	char szResp[SERIAL_BUFFER_SIZE];

	if(!m_bIsConnected)
		return NOT_CONNECTED;

    if(!m_bShutterPresent) {
        return SB_OK;
    }

	
	snprintf(szBuf, SERIAL_BUFFER_SIZE, "I%d#", nValue * 1000); // value is in ms
	nErr = domeCommand(szBuf, szResp, 'I', SERIAL_BUFFER_SIZE);
	return nErr;
}


int CRTIDome::getRainTimerValue(int &nValue)
{
    int nErr = ND_OK;
    char szResp[SERIAL_BUFFER_SIZE];

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    
    nErr = domeCommand("f#", szResp, 'f', SERIAL_BUFFER_SIZE);
    if(nErr) {
        return nErr;
    }

    nValue = atoi(szResp);
#ifdef PLUGIN_DEBUG
    ltime = time(NULL);
    timestamp = asctime(localtime(&ltime));
    timestamp[strlen(timestamp) - 1] = 0;
    fprintf(Logfile, "[%s] [CRTIDome::getRainTimerValue] nValue =  %d\n", timestamp, nValue);
    fflush(Logfile);
#endif
    return nErr;

}

int CRTIDome::setRainTimerValue(const int &nValue)
{
    int nErr = ND_OK;
    char szBuf[SERIAL_BUFFER_SIZE];
    char szResp[SERIAL_BUFFER_SIZE];

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    snprintf(szBuf, SERIAL_BUFFER_SIZE, "f%d#", nValue);
    nErr = domeCommand(szBuf, szResp, 'f', SERIAL_BUFFER_SIZE);
    return nErr;
}

int CRTIDome::getRainAction(int &nAction)
{
    int nErr = ND_OK;
    char szResp[SERIAL_BUFFER_SIZE];

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    nErr = domeCommand("n#", szResp, 'n', SERIAL_BUFFER_SIZE);
    if(nErr) {
        return nErr;
    }

    nAction = atoi(szResp);
#ifdef PLUGIN_DEBUG
    ltime = time(NULL);
    timestamp = asctime(localtime(&ltime));
    timestamp[strlen(timestamp) - 1] = 0;
    fprintf(Logfile, "[%s] [CRTIDome::getRainTimerValue] nValue =  %d\n", timestamp, nAction);
    fflush(Logfile);
#endif
    return nErr;

}

int CRTIDome::setRainAction(const int &nAction)
{
    int nErr = ND_OK;
    char szBuf[SERIAL_BUFFER_SIZE];
    char szResp[SERIAL_BUFFER_SIZE];

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    snprintf(szBuf, SERIAL_BUFFER_SIZE, "n%d#", nAction);
    nErr = domeCommand(szBuf, szResp, 'n', SERIAL_BUFFER_SIZE);
    return nErr;

}

int CRTIDome::getPanId(int &nPanId)
{
    int nErr = ND_OK;
    char szResp[SERIAL_BUFFER_SIZE];

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    nErr = domeCommand("q#", szResp, 'q', SERIAL_BUFFER_SIZE);
    if(nErr) {
        return nErr;
    }

    nPanId = int(strtol(szResp, NULL, 16));
#ifdef PLUGIN_DEBUG
    ltime = time(NULL);
    timestamp = asctime(localtime(&ltime));
    timestamp[strlen(timestamp) - 1] = 0;
    fprintf(Logfile, "[%s] [CRTIDome::getPanId] nPanId =  %04X\n", timestamp, nPanId);
    fflush(Logfile);
#endif

    return nErr;

}

int CRTIDome::setPanId(const int nPanId)
{
    int nErr = ND_OK;
    char szBuf[SERIAL_BUFFER_SIZE];
    char szResp[SERIAL_BUFFER_SIZE];

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    snprintf(szBuf, SERIAL_BUFFER_SIZE, "q%04X#", nPanId);
    nErr = domeCommand(szBuf, szResp, 'q', SERIAL_BUFFER_SIZE);
    return nErr;

}

int CRTIDome::getShutterPanId(int &nPanId)
{
    int nErr = ND_OK;
    char szResp[SERIAL_BUFFER_SIZE];

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    nErr = domeCommand("Q#", szResp, 'Q', SERIAL_BUFFER_SIZE);
    if(nErr) {
        return nErr;
    }

    nPanId = int(strtol(szResp, NULL, 16));
#ifdef PLUGIN_DEBUG
    ltime = time(NULL);
    timestamp = asctime(localtime(&ltime));
    timestamp[strlen(timestamp) - 1] = 0;
    fprintf(Logfile, "[%s] [CRTIDome::getShutterPanId] nPanId =  %04X\n", timestamp, nPanId);
    fflush(Logfile);
#endif

    return nErr;

}
int CRTIDome::isPanIdSet(const int nPanId, bool &bSet)
{
    int nErr = ND_OK;
    int nCtrlPanId;
    
    bSet = false;
    nErr = getShutterPanId(nCtrlPanId);
    if(nErr)
        return nErr;
    if(nCtrlPanId == nPanId)
        bSet = true;
    
    return nErr;
}


int CRTIDome::restoreDomeMotorSettings()
{
    int nErr = ND_OK;
    char szResp[SERIAL_BUFFER_SIZE];

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    nErr = domeCommand("d#", szResp, 'd', SERIAL_BUFFER_SIZE);
    if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        ltime = time(NULL);
        timestamp = asctime(localtime(&ltime));
        timestamp[strlen(timestamp) - 1] = 0;
        fprintf(Logfile, "[%s] [CRTIDome::restoreDomeMotorSettings] ERROR = %d\n", timestamp, nErr);
        fflush(Logfile);
#endif
    }

    return nErr;
}

int CRTIDome::restoreShutterMotorSettings()
{
    int nErr = ND_OK;
    char szResp[SERIAL_BUFFER_SIZE];
    int nDummy;
    
    if(!m_bIsConnected)
        return NOT_CONNECTED;

    nErr = domeCommand("D#", szResp, 'D', SERIAL_BUFFER_SIZE);
    if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        ltime = time(NULL);
        timestamp = asctime(localtime(&ltime));
        timestamp[strlen(timestamp) - 1] = 0;
        fprintf(Logfile, "[%s] [CRTIDome::restoreShutterMotorSettings] ERROR = %d\n", timestamp, nErr);
        fflush(Logfile);
#endif
    }
    nErr = getShutterAcceleration(nDummy);
    nErr |= getShutterSpeed(nDummy);
    return nErr;
}

void CRTIDome::enableRainStatusFile(bool bEnable)
{
    if(bEnable) {
        if(!RainStatusfile)
            RainStatusfile = fopen(m_sRainStatusfilePath.c_str(), "w");
        if(RainStatusfile) {
            m_bSaveRainStatus = true;
        }
        else { // if we failed to open the file.. don't log ..
            RainStatusfile = NULL;
            m_bSaveRainStatus = false;
        }
    }
    else {
        if(RainStatusfile) {
            fclose(RainStatusfile);
            RainStatusfile = NULL;
        }
        m_bSaveRainStatus = false;
    }
}

void CRTIDome::getRainStatusFileName(std::string &fName)
{
    fName.assign(m_sRainStatusfilePath);
}

void CRTIDome::writeRainStatus()
{
#ifdef PLUGIN_DEBUG
    ltime = time(NULL);
    timestamp = asctime(localtime(&ltime));
    timestamp[strlen(timestamp) - 1] = 0;
    fprintf(Logfile, "[%s] [CNexDomeV3::writeRainStatus] m_nIsRaining =  %s\n", timestamp, m_nIsRaining==RAINING?"Raining":"Not Raining");
    fprintf(Logfile, "[%s] [CNexDomeV3::writeRainStatus] m_bSaveRainStatus =  %s\n", timestamp, m_bSaveRainStatus?"YES":"NO");
    fflush(Logfile);
#endif

    if(m_bSaveRainStatus && RainStatusfile) {
        int nStatus;
        getRainSensorStatus(nStatus);
        fseek(RainStatusfile, 0, SEEK_SET);
        fprintf(RainStatusfile, "Raining:%s", nStatus == RAINING?"YES":"NO");
        fflush(RainStatusfile);
    }
}


int CRTIDome::parseFields(const char *pszResp, std::vector<std::string> &svFields, char cSeparator)
{
    int nErr = ND_OK;
    std::string sSegment;
    if(!pszResp) {
#ifdef PLUGIN_DEBUG
        ltime = time(NULL);
        timestamp = asctime(localtime(&ltime));
        timestamp[strlen(timestamp) - 1] = 0;
        fprintf(Logfile, "[%s] [CRTIDome::setDefaultDir] pszResp is NULL\n", timestamp);
        fflush(Logfile);
#endif
        return ERR_CMDFAILED;
    }

    if(!strlen(pszResp)) {
#ifdef PLUGIN_DEBUG
        ltime = time(NULL);
        timestamp = asctime(localtime(&ltime));
        timestamp[strlen(timestamp) - 1] = 0;
        fprintf(Logfile, "[%s] [CRTIDome::setDefaultDir] pszResp is enpty\n", timestamp);
        fflush(Logfile);
#endif
        return ERR_CMDFAILED;
    }
    std::stringstream ssTmp(pszResp);

    svFields.clear();
    // split the string into vector elements
    while(std::getline(ssTmp, sSegment, cSeparator))
    {
        svFields.push_back(sSegment);
    }

    if(svFields.size()==0) {
        nErr = ERR_CMDFAILED;
    }
    return nErr;
}

