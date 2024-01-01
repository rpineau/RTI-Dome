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

    m_fVersion = 0.0;
    m_fShutterVersion = 0.0;

    m_nHomingTries = 0;
    m_nGotoTries = 0;

    m_nIsRaining = NOT_RAINING;
    m_bSaveRainStatus = false;
    m_cRainCheckTimer.Reset();

    m_bHomeOnPark = false;
    m_bHomeOnUnpark = false;

    m_bShutterPresent = false;
    
    m_nRainStatus = RAIN_UNKNOWN;

    m_Port.clear();
    m_bNetworkConnected = false;
    
    m_IpAddress.clear();
    m_SubnetMask.clear();
    m_GatewayIP.clear();
    m_bUseDHCP = false;
    
    m_nShutterState = CLOSED;
    
    m_sFirmwareVersion.clear();
    m_sShutterFirmwareVersion.clear();

#ifdef PLUGIN_DEBUG
#if defined(WIN32)
    m_sLogfilePath = getenv("HOMEDRIVE");
    m_sLogfilePath += getenv("HOMEPATH");
    m_sLogfilePath += "\\RTI-Dome-Log.txt";
#else
    m_sLogfilePath = getenv("HOME");
    m_sLogfilePath += "/RTI-Dome-Log.txt";
#endif
    m_sLogFile.open(m_sLogfilePath, std::ios::out |std::ios::trunc);
#endif

#if defined(WIN32)
    m_sRainStatusfilePath = getenv("HOMEDRIVE");
    m_sRainStatusfilePath += getenv("HOMEPATH");
    m_sRainStatusfilePath += "\\RTI_Rain.txt";
#else
    m_sRainStatusfilePath = getenv("HOME");
    m_sRainStatusfilePath += "/RTI_Rain.txt";
#endif
    
#if defined PLUGIN_DEBUG
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [CRTIDome] Version " << std::fixed << std::setprecision(2) << PLUGIN_VERSION << " build " << __DATE__ << " " << __TIME__ << std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [CRTIDome] Constructor Called." << std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [CRTIDome] Rains status file : " << m_sRainStatusfilePath<<std::endl;
    m_sLogFile.flush();
#endif

}

CRTIDome::~CRTIDome()
{
#ifdef	PLUGIN_DEBUG
    // Close LogFile
    if(m_sLogFile.is_open())
        m_sLogFile.close();
#endif
}

int CRTIDome::Connect(const char *pszPort)
{
    int nErr;
    bool bDummy;
    
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [Connect] Called." << std::endl;
    m_sLogFile.flush();
#endif

    // 115200 8N1 DTR
    nErr = m_pSerx->open(pszPort, 115200, SerXInterface::B_NOPARITY, "-DTR_CONTROL 1");
    if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [Connect] Connection failed, nErr = " << nErr <<  std::endl;
        m_sLogFile.flush();
#endif
        m_bIsConnected = false;
        return nErr;
    }

    m_Port.assign(pszPort);
    
    m_bIsConnected = true;
    m_bCalibrating = false;
    m_bUnParking = false;

    if(m_Port.size()>=3 && m_Port.find("TCP")!= -1)  {
        m_bNetworkConnected = true;
    }
    else
        m_bNetworkConnected = false;

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [Connect] connected to " << pszPort << std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [Connect] connected via network : " << (m_bNetworkConnected?"Yes":"No") << std::endl;
    m_sLogFile.flush();
#endif

    nErr = getIpAddress(m_IpAddress);
    if(nErr)
        m_IpAddress = "";
    nErr |= getSubnetMask(m_SubnetMask);
    if(nErr)
        m_SubnetMask = "";
    nErr |= getIPGateway(m_GatewayIP);
    if(nErr)
        m_GatewayIP = "";
    nErr |= getUseDHCP(m_bUseDHCP);
    if(nErr)
        m_bUseDHCP = false;
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    if(nErr) {
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [Connect] Board without network feature." << std::endl;
        m_sLogFile.flush();
    }
#endif
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [Connect] Getting Firmware." << std::endl;
    m_sLogFile.flush();
#endif

    // if this fails we're not properly connected.
    nErr = getFirmwareVersion(m_sFirmwareVersion, m_fVersion);
    if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [Connect] Error Getting Firmware : " << nErr << std::endl;
        m_sLogFile.flush();
#endif
        m_bIsConnected = false;
        m_pSerx->close();
        return FIRMWARE_NOT_SUPPORTED;
    }

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [Connect]Got Firmware "<<  m_sFirmwareVersion << "( " << std::fixed << std::setprecision(2) << m_fVersion << ")."<< nErr << std::endl;
    m_sLogFile.flush();
#endif
    if(m_fVersion < 2.0f && m_fVersion != 0.523f && m_fVersion != 0.522f)  {
        return FIRMWARE_NOT_SUPPORTED;
    }

    nErr = getDomeParkAz(m_dCurrentAzPosition);
    if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [Connect] getDomeParkAz nErr : " << nErr << std::endl;
        m_sLogFile.flush();
#endif
        return nErr;
    }
    nErr = getDomeHomeAz(m_dHomeAz);
    if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [Connect] getDomeHomeAz nErr : " << nErr << std::endl;
        m_sLogFile.flush();
#endif
        return nErr;
    }

    sendShutterHello();
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    getShutterPresent(bDummy);
    // we need to get the initial state
    getShutterState(m_nShutterState);

    return PLUGIN_OK;
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

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [Disconnect] m_bIsConnected : " << (m_bIsConnected?"true":"false") << std::endl;
    m_sLogFile.flush();
#endif
}

int CRTIDome::deviceCommand(const std::string sCmd, std::string &sResp, char respCmdCode, int nTimeout, char cEndOfResponse)
{
    int nErr = PLUGIN_OK;
    unsigned long  ulBytesWrite;
    std::string localResp;

    if(!m_bIsConnected)
        return ERR_COMMNOLINK;

    m_pSerx->purgeTxRx();
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [deviceCommand] Sending : '" << sCmd <<  "'" << std::endl;
    m_sLogFile.flush();
#endif
    nErr = m_pSerx->writeFile((void *)(sCmd.c_str()), sCmd.size(), ulBytesWrite);
    m_pSerx->flushTx();

    if(nErr){
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [deviceCommand] writeFile error : " << nErr << std::endl;
        m_sLogFile.flush();
#endif
        return nErr;
    }

    if (respCmdCode == 0x00)
        return nErr;

    // read response
    nErr = readResponse(localResp, nTimeout, cEndOfResponse);
    if(nErr)
        return nErr;

    if(!localResp.size())
        return BAD_CMD_RESPONSE;

    if(localResp.at(0) != respCmdCode)
        nErr = BAD_CMD_RESPONSE;

    sResp = localResp.substr(1, localResp.size());

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [deviceCommand] response : " << sResp << std::endl;
    m_sLogFile.flush();
#endif

    return nErr;
}

int CRTIDome::readResponse(std::string &sResp, int nTimeout, char cEndOfResponse)
{
    int nErr = PLUGIN_OK;
    char pszBuf[SERIAL_BUFFER_SIZE];
    unsigned long ulBytesRead = 0;
    unsigned long ulTotalBytesRead = 0;
    char *pszBufPtr;
    int nBytesWaiting = 0 ;
    int nbTimeouts = 0;

    sResp.clear();
    memset(pszBuf, 0, SERIAL_BUFFER_SIZE);
    pszBufPtr = pszBuf;

    do {
        nErr = m_pSerx->bytesWaitingRx(nBytesWaiting);
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [readResponse] nBytesWaiting = " << nBytesWaiting << std::endl;
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [readResponse] nBytesWaiting nErr = " << nErr << std::endl;
        m_sLogFile.flush();
#endif
        if(!nBytesWaiting) {
            nbTimeouts += MAX_READ_WAIT_TIMEOUT;
            if(nbTimeouts >= nTimeout) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
                m_sLogFile << "["<<getTimeStamp()<<"]"<< " [readResponse] bytesWaitingRx timeout, no data for" << nbTimeouts <<" ms" << std::endl;
                m_sLogFile.flush();
#endif
                nErr = COMMAND_TIMEOUT;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(MAX_READ_WAIT_TIMEOUT));
            continue;
        }
        nbTimeouts = 0;
        if(ulTotalBytesRead + nBytesWaiting <= SERIAL_BUFFER_SIZE)
            nErr = m_pSerx->readFile(pszBufPtr, nBytesWaiting, ulBytesRead, nTimeout);
        else {
            nErr = ERR_RXTIMEOUT;
            break; // buffer is full.. there is a problem !!
        }
        if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [readResponse] readFile error." << std::endl;
            m_sLogFile.flush();
#endif
            return nErr;
        }

        if (ulBytesRead != nBytesWaiting) { // timeout
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [readResponse] readFile Timeout Error." << std::endl;
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [readResponse] readFile nBytesWaiting = " << nBytesWaiting << std::endl;
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [readResponse] readFile ulBytesRead =" << ulBytesRead << std::endl;
            m_sLogFile.flush();
#endif
        }

        ulTotalBytesRead += ulBytesRead;
        pszBufPtr+=ulBytesRead;
    } while (ulTotalBytesRead < SERIAL_BUFFER_SIZE  && *(pszBufPtr-1) != cEndOfResponse);


#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [readResponse] pszBuf = '" << pszBuf << "'" << std::endl;
    m_sLogFile.flush();
#endif


    if(!ulTotalBytesRead)
        nErr = COMMAND_TIMEOUT; // we didn't get an answer.. so timeout
    else
        *(pszBufPtr-1) = 0; //remove the cEndOfResponse

    sResp.assign(pszBuf);
    return nErr;
}


int CRTIDome::getDomeAz(double &dDomeAz)
{
    int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(m_bCalibrating)
        return nErr;

    ssCmd << GOTO_ROTATOR << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, GOTO_ROTATOR);
    if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getDomeAz] ERROR = " << sResp << std::endl;
        m_sLogFile.flush();
#endif
        return nErr;
    }
    // convert Az string to double
    try {
        dDomeAz = std::stof(sResp);
    }
    catch(const std::exception& e) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getDomeAz] conversion exception = " << e.what() << std::endl;
        m_sLogFile.flush();
#endif
        dDomeAz = 0;
    }

    m_dCurrentAzPosition = dDomeAz;

    if(m_cRainCheckTimer.GetElapsedSeconds() > RAIN_CHECK_INTERVAL) {
        writeRainStatus();
        m_cRainCheckTimer.Reset();
    }
    return nErr;
}

int CRTIDome::getDomeEl(double &dDomeEl)
{
    int nErr = PLUGIN_OK;

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

}


int CRTIDome::getDomeHomeAz(double &dAz)
{
    int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(m_bCalibrating)
        return nErr;

    ssCmd << HOMEAZ_ROTATOR << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, HOMEAZ_ROTATOR);
    if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getDomeHomeAz] ERROR = " << sResp << std::endl;
        m_sLogFile.flush();
#endif
        return nErr;
    }

    // convert Az string to double
    try {
        dAz = std::stof(sResp);
    }
    catch(const std::exception& e) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getDomeHomeAz] conversion exception = " << e.what() << std::endl;
        m_sLogFile.flush();
#endif
        dAz = 0;
    }

    m_dHomeAz = dAz;
    return nErr;
}

int CRTIDome::getDomeParkAz(double &dAz)
{
    int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(m_bCalibrating)
        return nErr;

    ssCmd << PARKAZ_ROTATOR << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, PARKAZ_ROTATOR);
    if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getDomeParkAz] ERROR = " << sResp << std::endl;
        m_sLogFile.flush();
#endif
        return nErr;
    }

    // convert Az string to double
    try {
        dAz = std::stof(sResp);
    }
    catch(const std::exception& e) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getDomeParkAz] conversion exception = " << e.what() << std::endl;
        m_sLogFile.flush();
#endif
        dAz = 0;
    }

    m_dParkAz = dAz;
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getDomeParkAz] m_dParkAz = " << std::fixed << std::setprecision(2) << m_dParkAz << std::endl;
    m_sLogFile.flush();
#endif

    return nErr;
}


int CRTIDome::getShutterState(int &nState)
{
    int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;
    std::vector<std::string> shutterStateFileds;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(!m_bShutterPresent) {
        nState = SHUTTER_ERROR;
        return nErr;
    }

    if(m_bCalibrating)
        return nErr;

    ssCmd << STATE_SHUTTER << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, STATE_SHUTTER);
    if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getShutterState] ERROR = " << sResp << std::endl;
        m_sLogFile.flush();
#endif
        nState = SHUTTER_ERROR;
        return nErr;
    }

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getShutterState] response =  " << sResp << std::endl;
    m_sLogFile.flush();
#endif

    try {
        nState = std::stoi(sResp);
    }
    catch(const std::exception& e) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getShutterState] conversion exception = " << e.what() << std::endl;
        m_sLogFile.flush();
#endif
        nState = 0;
    }

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getShutterState] nState =  " << nState << std::endl;
    m_sLogFile.flush();
#endif

    return nErr;
}


int CRTIDome::getDomeStepPerRev(int &nStepPerRev)
{
    int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    ssCmd << STEPSPER_ROTATOR << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, STEPSPER_ROTATOR);
    if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getDomeStepPerRev] ERROR = " << sResp << std::endl;
        m_sLogFile.flush();
#endif
        return nErr;
    }

    try {
        nStepPerRev = std::stoi(sResp);
    }
    catch(const std::exception& e) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getDomeStepPerRev] conversion exception = " << e.what() << std::endl;
        m_sLogFile.flush();
#endif
        nStepPerRev = 0;
    }
    m_nNbStepPerRev = nStepPerRev;
    return nErr;
}

int CRTIDome::setDomeStepPerRev(int nStepPerRev)
{
    int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;

    m_nNbStepPerRev = nStepPerRev;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    ssCmd << STEPSPER_ROTATOR << nStepPerRev  << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, STEPSPER_ROTATOR);
    return nErr;

}

int CRTIDome::getBatteryLevels(double &domeVolts, double &dDomeCutOff, double &dShutterVolts, double &dShutterCutOff)
{
    int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;
    std::vector<std::string> voltsFields;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(m_bCalibrating)
        return nErr;
    // Dome
    ssCmd << VOLTS_ROTATOR << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, VOLTS_ROTATOR);
    if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getBatteryLevels] ERROR = " << sResp << std::endl;
        m_sLogFile.flush();
#endif
        return nErr;
    }

    nErr = parseFields(sResp, voltsFields, ',');
    if(nErr) {
        return PLUGIN_OK;
    }
    if(!voltsFields.size()) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getBatteryLevels] voltsFields is empty" << std::endl;
        m_sLogFile.flush();
#endif
        return MAKE_ERR_CODE(PLUGIN_ID, DriverRootInterface::DT_DOME, ERR_CMDFAILED);
    }

    if(voltsFields.size()>1) {
        try {
            domeVolts = std::stof(voltsFields[0]);
            dDomeCutOff = std::stof(voltsFields[1]);
        }
        catch(const std::exception& e) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getBatteryLevels] conversion exception = " << e.what() << std::endl;
            m_sLogFile.flush();
#endif
            domeVolts = 0;
            dDomeCutOff = 0;
        }
    }
    else {
        domeVolts = 0;
        dDomeCutOff = 0;
        return MAKE_ERR_CODE(PLUGIN_ID, DriverRootInterface::DT_DOME, ERR_CMDFAILED);
    }

    domeVolts = domeVolts / 100.0;
    dDomeCutOff = dDomeCutOff / 100.0;

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getBatteryLevels] domeVolts = " << std::fixed << std::setprecision(2) << domeVolts << std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getBatteryLevels] dDomeCutOff = " << std::fixed << std::setprecision(2) << dDomeCutOff << std::endl;
    m_sLogFile.flush();
#endif

    dShutterVolts  = 0;
    dShutterCutOff = 0;
    if(m_bShutterPresent) {
        std::stringstream().swap(ssCmd);
        ssCmd << VOLTS_SHUTTER << "#";
        nErr = deviceCommand(ssCmd.str(), sResp, VOLTS_SHUTTER);
        if(nErr) {
    #if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getBatteryLevels] ERROR = " << sResp << std::endl;
            m_sLogFile.flush();
    #endif
            dShutterVolts = -1;
            dShutterCutOff = -1;
            return nErr;
        }
        nErr = parseFields(sResp, voltsFields, ',');

        if(!voltsFields.size()) { // no shutter value
            dShutterVolts = -1;
            dShutterCutOff = -1;
            return nErr;
        }

        try {
            dShutterVolts = std::stof(voltsFields[0]);
            dShutterCutOff = std::stof(voltsFields[1]);
        }
        catch(const std::exception& e) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getBatteryLevels] conversion exception = " << e.what() << std::endl;
            m_sLogFile.flush();
#endif
            dShutterVolts = 0;
            dShutterCutOff = 0;
        }

        dShutterVolts = dShutterVolts / 100.0;
        dShutterCutOff = dShutterCutOff / 100.0;
    #if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getBatteryLevels] shutterVolts = " << std::fixed << std::setprecision(2) << dShutterVolts << std::endl;
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getBatteryLevels] dShutterCutOff = " << std::fixed << std::setprecision(2) << dShutterCutOff << std::endl;
        m_sLogFile.flush();
    #endif
    }

    return nErr;
}

int CRTIDome::setBatteryCutOff(double dDomeCutOff, double dShutterCutOff)
{
    int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;
    int nRotCutOff, nShutCutOff;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(m_bCalibrating)
        return nErr;

    nRotCutOff = dDomeCutOff * 100.0;
    nShutCutOff = dShutterCutOff * 100.0;

    // Dome
    ssCmd << VOLTS_ROTATOR << nRotCutOff  << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, VOLTS_ROTATOR);
   if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [setBatteryCutOff] dDomeCutOff ERROR = " << sResp << std::endl;
        m_sLogFile.flush();
#endif
        return nErr;
    }

    if(m_bShutterPresent) {
        // Shutter
        std::stringstream().swap(ssCmd);
        ssCmd << VOLTS_SHUTTER << nRotCutOff  << "#";
        nErr = deviceCommand(ssCmd.str(), sResp, VOLTS_SHUTTER);
        if(nErr) {
    #if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [setBatteryCutOff] dDomeCutOff ERROR = " << sResp << std::endl;
            m_sLogFile.flush();
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
    int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    ssCmd << SLEW_ROTATOR  << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, SLEW_ROTATOR);
    if(nErr & !m_bCalibrating) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [isDomeMoving] ERROR = " << sResp << std::endl;
        m_sLogFile.flush();
#endif
        return false;
    }
    else if (nErr & m_bCalibrating) {
        return true;
    }

    bIsMoving = false;
    try {
        nTmp = std::stoi(sResp);
    }
    catch(const std::exception& e) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [isDomeMoving] conversion exception = " << e.what() << std::endl;
        m_sLogFile.flush();
#endif
        nTmp = MOVE_NONE;
    }
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [isDomeMoving] nTmp : " << nTmp << std::endl;
    m_sLogFile.flush();
#endif
    if(nTmp != MOVE_NONE)
        bIsMoving = true;

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [isDomeMoving] bIsMoving : " << (bIsMoving?"True":"False") << std::endl;
    m_sLogFile.flush();
#endif

    return bIsMoving;
}

bool CRTIDome::isDomeAtHome()
{
    bool bAthome;
    int nTmp;
    int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    ssCmd << HOMESTATUS_ROTATOR  << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, HOMESTATUS_ROTATOR);
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [isDomeAtHome] response = " << sResp << std::endl;
    m_sLogFile.flush();
#endif
    if(nErr) {
        return false;
    }

    bAthome = false;
    try {
        nTmp = std::stoi(sResp);
    }
    catch(const std::exception& e) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [isDomeAtHome] conversion exception = " << e.what() << std::endl;
        m_sLogFile.flush();
#endif
        nTmp = ATHOME;
    }
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [isDomeAtHome] nTmp : " << nTmp << std::endl;
    m_sLogFile.flush();
#endif
    if(nTmp == ATHOME)
        bAthome = true;
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [isDomeAtHome] bAthome : " << (bAthome?"True":"False") << std::endl;
    m_sLogFile.flush();
#endif

    return bAthome;
}

int CRTIDome::syncDome(double dAz, double dEl)
{
    int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    m_dCurrentAzPosition = dAz;

    ssCmd << SYNC_ROTATOR << std::fixed << std::setprecision(2) << dAz << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, SYNC_ROTATOR);
    if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [syncDome] ERROR = " << sResp << std::endl;
        m_sLogFile.flush();
#endif
        return nErr;
    }
    // TODO : Also set Elevation when supported by the firmware.
    // m_dCurrentElPosition = dEl;
    return nErr;
}

int CRTIDome::parkDome()
{
    int nErr = PLUGIN_OK;

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
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [unparkDome] m_dParkAz = " << std::fixed << std::setprecision(2) << m_dParkAz << std::endl;
        m_sLogFile.flush();
#endif
        syncDome(m_dParkAz, m_dCurrentElPosition);
        m_bParked = false;
        m_bUnParking = false;
    }

    return 0;
}

int CRTIDome::gotoAzimuth(double dNewAz)
{
    int nErr = PLUGIN_OK;
    double domeVolts;
    double dDomeCutOff;
    double dShutterVolts;
    double dShutterCutOff;
    bool bDummy;
    std::stringstream ssCmd;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    getShutterPresent(bDummy);
    if(m_bShutterPresent) {
        getBatteryLevels(domeVolts, dDomeCutOff, dShutterVolts, dShutterCutOff);
        if(dShutterVolts < dShutterCutOff)
            return ERR_DEVICEPARKED; // dome has parked to charge the shutter battery, don't move !
    }
    while(dNewAz >= 360)
        dNewAz = dNewAz - 360;

    ssCmd << GOTO_ROTATOR << std::fixed << std::setprecision(2) << dNewAz << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, GOTO_ROTATOR);
    if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [gotoAzimuth] ERROR = " << sResp << std::endl;
        m_sLogFile.flush();
#endif
        return nErr;
    }

    m_dGotoAz = dNewAz;
    return nErr;
}

int CRTIDome::openShutter()
{
    int nErr = PLUGIN_OK;
    bool bDummy;
    std::stringstream ssCmd;
    std::string sResp;
    double domeVolts;
    double dDomeCutOff;
    double dShutterVolts;
    double dShutterCutOff;
    
    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(m_bCalibrating)
        return PLUGIN_OK;
    
    getShutterPresent(bDummy);
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [openShutter] m_bShutterPresent : " << (m_bShutterPresent?"True":"False") << std::endl;
    m_sLogFile.flush();
#endif
    if(!m_bShutterPresent) {
        return PLUGIN_OK;
    }

    getBatteryLevels(domeVolts, dDomeCutOff, dShutterVolts, dShutterCutOff);
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [openShutter] Opening shutter" << std::endl;
    m_sLogFile.flush();
#endif

    ssCmd << OPEN_SHUTTER  << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, OPEN_SHUTTER);
    if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [openShutter] ERROR = " << nErr << std::endl;
        m_sLogFile.flush();
#endif
    }

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [openShutter] response = " << sResp << std::endl;
    m_sLogFile.flush();
#endif

    if(sResp.size() && sResp.at(0) == 'L') { // battery LOW.. can't open
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [openShutter] Voltage too low to open" << std::endl;
        m_sLogFile.flush();
#endif
        nErr = MAKE_ERR_CODE(PLUGIN_ID, DriverRootInterface::DT_DOME, ERR_BATTERY_LOW);
    }
    if(sResp.size() && sResp.at(0) == 'R') { // Raining. can't open
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [openShutter] Voltage too low to open" << std::endl;
        m_sLogFile.flush();
#endif
        nErr = MAKE_ERR_CODE(PLUGIN_ID, DriverRootInterface::DT_DOME, ERR_RAINING);
    }

    return nErr;
}

int CRTIDome::closeShutter()
{
    int nErr = PLUGIN_OK;
    bool bDummy;
    std::stringstream ssCmd;
    std::string sResp;
    double domeVolts;
    double dDomeCutOff;
    double dShutterVolts;
    double dShutterCutOff;
    
    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(m_bCalibrating)
        return PLUGIN_OK;

    getShutterPresent(bDummy);
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [closeShutter] m_bShutterPresent = " << (m_bShutterPresent?"Yes":"No") << std::endl;
    m_sLogFile.flush();
#endif

    if(!m_bShutterPresent) {
        return PLUGIN_OK;
    }

    getBatteryLevels(domeVolts, dDomeCutOff, dShutterVolts, dShutterCutOff);

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [closeShutter] Closing shutter" << std::endl;
    m_sLogFile.flush();
#endif

    ssCmd << CLOSE_SHUTTER  << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, CLOSE_SHUTTER);
    if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [closeShutter] ERROR Closing shutter : " << nErr << std::endl;
        m_sLogFile.flush();
#endif
    }

    if(sResp.size() && sResp.at(0) == 'L') { // batteryb LOW.. can't close :(
        nErr = MAKE_ERR_CODE(PLUGIN_ID, DriverRootInterface::DT_DOME, ERR_BATTERY_LOW);
    }

    return nErr;
}

int CRTIDome::getFirmwareVersion(std::string &sVersion, float &fVersion)
{
    int nErr = PLUGIN_OK;
    int i;
    std::stringstream ssCmd;
    std::string sResp;
    std::vector<std::string> firmwareFields;
    std::vector<std::string> versionFields;
    std::string strVersion;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(m_bCalibrating)
        return PLUGIN_OK;

    ssCmd << VERSION_ROTATOR  << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, VERSION_ROTATOR);
    if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getFirmwareVersion] ERROR = " << sResp << std::endl;
        m_sLogFile.flush();
#endif
        return MAKE_ERR_CODE(PLUGIN_ID, DriverRootInterface::DT_DOME, ERR_CMDFAILED);
    }

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getFirmwareVersion] response = " << sResp << std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getFirmwareVersion] response len = " << sResp.size() << std::endl;
    m_sLogFile.flush();
#endif

    nErr = parseFields(sResp,firmwareFields, 'v');
    if(nErr) {
        sVersion = "N/A";
        fVersion = 0.0;
        return PLUGIN_OK;
    }
    if(!firmwareFields.size()) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getFirmwareVersion] firmwareFields is empty" << std::endl;
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getFirmwareVersion] response len = " << sResp.size() << std::endl;
        m_sLogFile.flush();
#endif
        return MAKE_ERR_CODE(PLUGIN_ID, DriverRootInterface::DT_DOME, ERR_CMDFAILED);
    }

    if(firmwareFields.size()>0) {
        nErr = parseFields(firmwareFields[0],versionFields, '.');
        if(versionFields.size()>1) {
            strVersion=versionFields[0]+".";
            for(i=1; i<versionFields.size(); i++) {
                strVersion+=versionFields[i];
            }
            sVersion.assign(sResp);
            try {
                fVersion = std::stof(strVersion);
            }
            catch(const std::exception& e) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
                m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getFirmwareVersion] conversion exception = " << e.what() << std::endl;
                m_sLogFile.flush();
#endif
                fVersion = 0;
            }
        }
        else {
            sVersion.assign(sResp);
            fVersion = 0.0;
        }
    }
    return nErr;
}

int CRTIDome::getFirmwareVersion(float &fVersion)
{
    int nErr = PLUGIN_OK;

    if(m_fVersion == 0.0f) {
        nErr = getFirmwareVersion(m_sFirmwareVersion, m_fVersion);
        if(nErr)
            return nErr;
    }

    fVersion = m_fVersion;

    return nErr;
}

int CRTIDome::getShutterFirmwareVersion(std::string &sVersion, float &fVersion)
{
    int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;
    std::vector<std::string> firmwareFields;
    std::vector<std::string> versionFields;
    std::string strVersion;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(m_bCalibrating)
        return PLUGIN_OK;

    ssCmd << VERSION_SHUTTER  << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, VERSION_SHUTTER);
    if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getShutterFirmwareVersion] ERROR = " << sResp << std::endl;
        m_sLogFile.flush();
#endif
        return nErr;
    }

    nErr = parseFields(sResp,firmwareFields, 'V');
    if(nErr) {
        sVersion = "N/A";
        fVersion = 0.0;
        return PLUGIN_OK;
    }

    if(firmwareFields.size()>0) {
        try {
            sVersion.assign(firmwareFields[0]);
            fVersion = std::stof(firmwareFields[0]);
        }
        catch(const std::exception& e) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getShutterFirmwareVersion] conversion exception = " << e.what() << std::endl;
            m_sLogFile.flush();
#endif
            fVersion = 0;
        }
    }
    else {
        sVersion = "N/A";
        fVersion = 0.0;
    }
    return nErr;
}

int CRTIDome::goHome()
{
    int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(m_bCalibrating) {
        return PLUGIN_OK;
    }
    else if(isDomeAtHome()){
            return PLUGIN_OK;
    }
#ifdef PLUGIN_DEBUG
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [goHome]" << std::endl;
    m_sLogFile.flush();
#endif

    m_nHomingTries = 0;
    ssCmd << HOME_ROTATOR  << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, HOME_ROTATOR);
    if(nErr) {
#ifdef PLUGIN_DEBUG
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [goHome] ERROR = " << nErr << std::endl;
        m_sLogFile.flush();
#endif
        return nErr;
    }

    return nErr;
}

int CRTIDome::calibrate()
{
    int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    ssCmd << CALIBRATE_ROTATOR  << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, CALIBRATE_ROTATOR);
    if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [calibrate] ERROR = " << nErr << std::endl;
        m_sLogFile.flush();
#endif
        return nErr;
    }
    m_bCalibrating = true;

    return nErr;
}

int CRTIDome::isGoToComplete(bool &bComplete)
{
    int nErr = PLUGIN_OK;
    double dDomeAz = 0;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    bComplete = false;
    if(isDomeMoving()) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [isGoToComplete] Dome is still moving" << std::endl;
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [isGoToComplete] bComplete = " << (bComplete?"True":"False") << std::endl;
        m_sLogFile.flush();
#endif
        return nErr;
    }

    getDomeAz(dDomeAz);

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [isGoToComplete] DomeAz = " << std::fixed << std::setprecision(2) << dDomeAz << std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [isGoToComplete] m_dGotoAz = " << std::fixed << std::setprecision(2) << m_dGotoAz << std::endl;
    m_sLogFile.flush();
#endif

    if(checkBoundaries(m_dGotoAz, dDomeAz)) {
        bComplete = true;
        m_nGotoTries = 0;
    }
    else {
        // we're not moving and we're not at the final destination !!!
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [isGoToComplete] ***** ERROR **** domeAz = " << std::fixed << std::setprecision(2) << dDomeAz << ", m_dGotoAz =" << std::fixed << std::setprecision(2) << m_dGotoAz << std::endl;
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [isGoToComplete] m_dGotoAz = " << std::fixed << std::setprecision(2) << m_dGotoAz << std::endl;
        m_sLogFile.flush();
#endif
        if(m_nGotoTries == 0) {
            bComplete = false;
            m_nGotoTries = 1;
            gotoAzimuth(m_dGotoAz);
        }
        else {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [isGoToComplete] After retry ***** ERROR **** domeAz = " << std::fixed << std::setprecision(2) << dDomeAz << ", m_dGotoAz =" << std::fixed << std::setprecision(2) << m_dGotoAz << std::endl;
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [isGoToComplete] After retry m_dGotoAz = " << std::fixed << std::setprecision(2) << m_dGotoAz << std::endl;
            m_sLogFile.flush();
#endif
            m_nGotoTries = 0;
            nErr = ERR_CMDFAILED;
        }
    }

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [isGoToComplete] bComplete = " << (bComplete?"True":"False") << std::endl;
    m_sLogFile.flush();
#endif

    return nErr;
}


bool CRTIDome::checkBoundaries(double dTargetAz, double dDomeAz, double nMargin)
{
    double highMark;
    double lowMark;
    double roundedGotoAz;

    // we need to test "large" depending on the heading error and movement coasting
    highMark = ceil(dDomeAz)+nMargin;
    lowMark = ceil(dDomeAz)-nMargin;
    roundedGotoAz = ceil(dTargetAz);

    if(lowMark < 0 && highMark > 0) { // we're close to 0 degre but above 0
        if((roundedGotoAz+2) >= 360)
            roundedGotoAz = (roundedGotoAz+2)-360;
        if ( (roundedGotoAz > lowMark) && (roundedGotoAz <= highMark)) {
            return true;
        }
    }
    if ( lowMark > 0 && highMark>360 ) { // we're close to 0 but from the other side
        if( (roundedGotoAz+360) > lowMark && (roundedGotoAz+360) <= highMark) {
            return true;
        }
    }
    if (roundedGotoAz > lowMark && roundedGotoAz <= highMark) {
        return true;
    }

    return false;
}


int CRTIDome::isOpenComplete(bool &bComplete)
{
    int nErr = PLUGIN_OK;
    bool bDummy;
    if(!m_bIsConnected)
        return NOT_CONNECTED;

    getShutterPresent(bDummy);
    if(!m_bShutterPresent) {
        bComplete = true; // no shuter, report open by default
        return PLUGIN_OK;
    }

    nErr = getShutterState(m_nShutterState);
    if(nErr)
        return MAKE_ERR_CODE(PLUGIN_ID, DriverRootInterface::DT_DOME, ERR_CMDFAILED);
    if(m_nShutterState == OPEN){
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
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [isOpenComplete] bComplete = " << (bComplete?"True":"False") << std::endl;
    m_sLogFile.flush();
#endif

    return nErr;
}

int CRTIDome::isCloseComplete(bool &bComplete)
{
    int nErr = PLUGIN_OK;
    bool bDummy;
    
    if(!m_bIsConnected)
        return NOT_CONNECTED;

    getShutterPresent(bDummy);
    if(!m_bShutterPresent) {
        bComplete = false; // no shuter, report open by default
        return PLUGIN_OK;
    }

    nErr = getShutterState(m_nShutterState);
    if(nErr)
        return MAKE_ERR_CODE(PLUGIN_ID, DriverRootInterface::DT_DOME, ERR_CMDFAILED);
    if(m_nShutterState == CLOSED){
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
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [isCloseComplete] bComplete = " << (bComplete?"True":"False") << std::endl;
    m_sLogFile.flush();
#endif

    return nErr;
}


int CRTIDome::isParkComplete(bool &bComplete)
{
    int nErr = PLUGIN_OK;
    double dDomeAz=0;
    bool bFoundHome;

    if(!m_bIsConnected)
        return NOT_CONNECTED;
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [isParkComplete] m_bParking = " << (m_bParking?"True":"False") << std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [isParkComplete] bComplete = " << (bComplete?"True":"False") << std::endl;
    m_sLogFile.flush();
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
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [isParkComplete] found home, now parking" << std::endl;
            m_sLogFile.flush();
#endif
            m_bParking = false;
            nErr = gotoAzimuth(m_dParkAz);
        }
        return nErr;
    }

    getDomeAz(dDomeAz);

    if(checkBoundaries(m_dParkAz, dDomeAz)) {
        m_bParked = true;
        bComplete = true;
    }
    else {
        // we're not moving and we're not at the final destination !!!
        bComplete = false;
        m_bParked = false;
        nErr = MAKE_ERR_CODE(PLUGIN_ID, DriverRootInterface::DT_DOME, ERR_CMDFAILED);
    }

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [isParkComplete] bComplete = " << (bComplete?"True":"False") << std::endl;
    m_sLogFile.flush();
#endif

    return nErr;
}

int CRTIDome::isUnparkComplete(bool &bComplete)
{
    int nErr = PLUGIN_OK;

    bComplete = false;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(!m_bParked) {
        bComplete = true;
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [isUnparkComplete] UNPARKED" << std::endl;
        m_sLogFile.flush();
#endif
    }
    else if (m_bUnParking) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [isUnparkComplete] unparking.. checking if we're home" << std::endl;
        m_sLogFile.flush();
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
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [isUnparkComplete] m_bParked = " << (m_bParked?"True":"False") << std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [isUnparkComplete] bComplete = " << (bComplete?"True":"False") << std::endl;
    m_sLogFile.flush();
#endif

    return nErr;
}

int CRTIDome::isFindHomeComplete(bool &bComplete)
{
    int nErr = PLUGIN_OK;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

#ifdef PLUGIN_DEBUG
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [isFindHomeComplete]" << std::endl;
    m_sLogFile.flush();
#endif

    if(isDomeMoving()) {
        bComplete = false;
#ifdef PLUGIN_DEBUG
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [isFindHomeComplete] still moving" << std::endl;
        m_sLogFile.flush();
#endif
        return nErr;
    }

    if(isDomeAtHome()){
        bComplete = true;
        if(m_bUnParking)
            m_bParked = false;
        syncDome(m_dHomeAz, m_dCurrentElPosition);
        m_nHomingTries = 0;
#ifdef PLUGIN_DEBUG
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [isFindHomeComplete] At Home" << std::endl;
        m_sLogFile.flush();
#endif
    }
    else {
        // we're not moving and we're not at the home position !!!
#ifdef PLUGIN_DEBUG
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [isFindHomeComplete] Not moving and not at home !!!" << std::endl;
        m_sLogFile.flush();
#endif
        bComplete = false;
        m_bParked = false;
        // sometimes we pass the home sensor and we don't detect it.
        // so give it another try
        if(m_nHomingTries == 0) {
            m_nHomingTries = 1;
            goHome();
        }
        else {
            m_nHomingTries = 0;
            nErr = MAKE_ERR_CODE(PLUGIN_ID, DriverRootInterface::DT_DOME, ERR_CMDFAILED);
        }
    }

    return nErr;
}


int CRTIDome::isCalibratingComplete(bool &bComplete)
{
    int nErr = PLUGIN_OK;
    double dDomeAz = 0;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(isDomeMoving()) {
        bComplete = false;
        return nErr;
    }


    nErr = getDomeAz(dDomeAz);

    if (ceil(m_dHomeAz) != ceil(dDomeAz)) {
        // We need to resync the current position to the home position.
        m_dCurrentAzPosition = m_dHomeAz;
        syncDome(m_dCurrentAzPosition,m_dCurrentElPosition);
        bComplete = true;
    }

    nErr = getDomeStepPerRev(m_nNbStepPerRev);
    bComplete = true;
    m_bCalibrating = false;
#ifdef PLUGIN_DEBUG
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [isCalibratingComplete] m_nNbStepPerRev = " << m_nNbStepPerRev << std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [isCalibratingComplete] m_bCalibrating = " << (m_bCalibrating?"True":"False") << std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [isCalibratingComplete] bComplete = " << (bComplete?"True":"False") << std::endl;
    m_sLogFile.flush();
#endif
    return nErr;
}


int CRTIDome::abortCurrentCommand()
{
    int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    m_bParked = false;
    m_bCalibrating = false;
    m_bParking = false;
    m_bUnParking = false;
    m_nGotoTries = 1;   // prevents the goto retry
    m_nHomingTries = 1; // prevents the find home retry

    ssCmd << ABORT  << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, ABORT);

    getDomeAz(m_dGotoAz);

    return nErr;
}

int CRTIDome::sendShutterHello()
{
    int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    ssCmd << HELLO  << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, HELLO);
    return nErr;
}

int CRTIDome::getShutterPresent(bool &bShutterPresent)
{
    int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;

    ssCmd << IS_SHUTTER_PRESENT  << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, IS_SHUTTER_PRESENT);
    if(nErr) {
        return nErr;
    }
    m_bShutterPresent = false;
    if(sResp.size())
        m_bShutterPresent = (sResp.at(0)=='1') ? true : false;
#ifdef PLUGIN_DEBUG
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getShutterPresent] sResp = " << sResp << std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getShutterPresent] m_bShutterPresent = " << (m_bShutterPresent?"True":"False") << std::endl;
    m_sLogFile.flush();
#endif

    bShutterPresent = m_bShutterPresent;
    if(m_bShutterPresent && m_sShutterFirmwareVersion.size() == 0)
        getShutterFirmwareVersion(m_sShutterFirmwareVersion, m_fShutterVersion);
    
    return nErr;
}

#pragma mark - Getter / Setter

int CRTIDome::getNbTicksPerRev()
{
    if(m_bIsConnected)
        getDomeStepPerRev(m_nNbStepPerRev);

#ifdef PLUGIN_DEBUG
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getNbTicksPerRev] m_nNbStepPerRev = " << m_nNbStepPerRev << std::endl;
    m_sLogFile.flush();
#endif

    return m_nNbStepPerRev;
}

int CRTIDome::setNbTicksPerRev(int nSteps)
{
    int nErr = PLUGIN_OK;

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
    int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;
    m_dHomeAz = dAz;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    ssCmd << HOMEAZ_ROTATOR << std::fixed << std::setprecision(2) << dAz << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, HOMEAZ_ROTATOR);
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
    int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;

    m_dParkAz = dAz;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    ssCmd << PARKAZ_ROTATOR << std::fixed << std::setprecision(2) << dAz << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, PARKAZ_ROTATOR);
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


int CRTIDome::getDefaultDir(bool &bNormal)
{
    int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;

    bNormal = true;

    ssCmd << REVERSED_ROTATOR  << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, REVERSED_ROTATOR);
    if(nErr) {
        return nErr;
    }
    try {
        bNormal = std::stoi(sResp) ? false:true;
    }
    catch(const std::exception& e) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getDefaultDir] conversion exception = " << e.what() << std::endl;
        m_sLogFile.flush();
#endif
        bNormal = true;
    }

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getDefaultDir] bNormal = " << (bNormal?"True":"False") << std::endl;
    m_sLogFile.flush();
#endif

    return nErr;
}

int CRTIDome::setDefaultDir(bool bNormal)
{
    int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;
    std::stringstream ssTmp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    ssCmd << REVERSED_ROTATOR << (bNormal?"0":"1") << "#";

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [setDefaultDir] bNormal = " << (bNormal?"True":"False") << std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [setDefaultDir] ssTmp = " << ssCmd.str() << std::endl;
    m_sLogFile.flush();
#endif

    nErr = deviceCommand(ssCmd.str(), sResp, REVERSED_ROTATOR);
    return nErr;

}

int CRTIDome::getRainSensorStatus(int &nStatus)
{
    int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;

    nStatus = NOT_RAINING;

    ssCmd << RAIN_SHUTTER  << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, RAIN_SHUTTER);
    if(nErr) {
        return nErr;
    }

    try {
        nStatus = std::stoi(sResp) ? false:true;
    }
    catch(const std::exception& e) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getRainSensorStatus] conversion exception = " << e.what() << std::endl;
        m_sLogFile.flush();
#endif
        nStatus = false;
    }

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getRainSensorStatus] nStatus = " << (nStatus?"NOT RAINING":"RAINING") << std::endl;
    m_sLogFile.flush();
#endif

    m_nIsRaining = nStatus;
    return nErr;
}

int CRTIDome::getRotationSpeed(int &nSpeed)
{
    int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    ssCmd << SPEED_ROTATOR  << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, SPEED_ROTATOR);
    if(nErr) {
        return nErr;
    }

    try{
        nSpeed = std::stoi(sResp);
    }
    catch(const std::exception& e) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getRotationSpeed] conversion exception = " << e.what() << std::endl;
        m_sLogFile.flush();
#endif
        nSpeed = 0;
    }

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getRotationSpeed] nSpeed = " << nSpeed << std::endl;
    m_sLogFile.flush();
#endif

    return nErr;
}

int CRTIDome::setRotationSpeed(int nSpeed)
{
    int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    ssCmd << SPEED_ROTATOR << nSpeed << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, SPEED_ROTATOR);
    return nErr;
}


int CRTIDome::getRotationAcceleration(int &nAcceleration)
{
    int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    ssCmd << ACCELERATION_ROTATOR  << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, ACCELERATION_ROTATOR);
    if(nErr) {
        return nErr;
    }

    try {
        nAcceleration = std::stoi(sResp);
    }
    catch(const std::exception& e) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getRotationAcceleration] conversion exception = " << e.what() << std::endl;
        m_sLogFile.flush();
#endif
        nAcceleration = 0;
    }
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getRotationAcceleration] nAcceleration = " << nAcceleration << std::endl;
    m_sLogFile.flush();
#endif

    return nErr;
}

int CRTIDome::setRotationAcceleration(int nAcceleration)
{
    int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    ssCmd << ACCELERATION_ROTATOR << nAcceleration << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, ACCELERATION_ROTATOR);

    return nErr;
}

int CRTIDome::getShutterSpeed(int &nSpeed)
{
    int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(!m_bShutterPresent) {
        nSpeed = 0;
        return PLUGIN_OK;
    }

    ssCmd << SPEED_SHUTTER  << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, SPEED_SHUTTER);
    if(nErr) {
        return nErr;
    }

    try {
        nSpeed = std::stoi(sResp);
    }
    catch(const std::exception& e) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getShutterSpeed] conversion exception = " << e.what() << std::endl;
        m_sLogFile.flush();
#endif
        nSpeed = 0;
    }
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getShutterSpeed] nSpeed = " << nSpeed << std::endl;
    m_sLogFile.flush();
#endif

    return nErr;
}

int CRTIDome::setShutterSpeed(int nSpeed)
{
    int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(!m_bShutterPresent) {
        return PLUGIN_OK;
    }

    ssCmd << SPEED_SHUTTER << nSpeed << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, SPEED_SHUTTER);

    return nErr;
}

int CRTIDome::getShutterAcceleration(int &nAcceleration)
{
    int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(!m_bShutterPresent) {
        nAcceleration = 0;
        return PLUGIN_OK;
    }

    ssCmd << ACCELERATION_SHUTTER  << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, ACCELERATION_SHUTTER);
    if(nErr) {
        return nErr;
    }

    try {
        nAcceleration = std::stoi(sResp);
    }
    catch(const std::exception& e) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getShutterAcceleration] conversion exception = " << e.what() << std::endl;
        m_sLogFile.flush();
#endif
        nAcceleration = 0;
    }
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getShutterAcceleration] nAcceleration = " << nAcceleration << std::endl;
    m_sLogFile.flush();
#endif
    return nErr;
}

int CRTIDome::setShutterAcceleration(int nAcceleration)
{
    int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(!m_bShutterPresent) {
        return PLUGIN_OK;
    }

    ssCmd << ACCELERATION_SHUTTER << nAcceleration << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, ACCELERATION_SHUTTER);
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
	int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;

	if(!m_bIsConnected)
		return NOT_CONNECTED;

    if(!m_bShutterPresent) {
        nValue = 0;
        return PLUGIN_OK;
    }

    ssCmd << WATCHDOG_INTERVAL  << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, WATCHDOG_INTERVAL);
	if(nErr) {
		return nErr;
	}

    try {
        nValue = std::stoi(sResp)/1000; // value is in ms
    }
    catch(const std::exception& e) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getSutterWatchdogTimerValue] conversion exception = " << e.what() << std::endl;
        m_sLogFile.flush();
#endif
        nValue = 0;
    }

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getSutterWatchdogTimerValue] nValue = " << nValue << std::endl;
    m_sLogFile.flush();
#endif
	return nErr;
}

int	CRTIDome::setSutterWatchdogTimerValue(const int &nValue)
{
	int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;

	if(!m_bIsConnected)
		return NOT_CONNECTED;

    if(!m_bShutterPresent) {
        return PLUGIN_OK;
    }

    ssCmd << WATCHDOG_INTERVAL << (nValue * 1000) << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, WATCHDOG_INTERVAL);
	return nErr;
}

int CRTIDome::getRainAction(int &nAction)
{
    int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    ssCmd << RAIN_ROTATOR_ACTION  << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, RAIN_ROTATOR_ACTION);
    if(nErr) {
        return nErr;
    }

    try {
        nAction = std::stoi(sResp);
    }
    catch(const std::exception& e) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getRainAction] conversion exception = " << e.what() << std::endl;
        m_sLogFile.flush();
#endif
        nAction = 0;
    }

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getRainTimerValue] nAction = " << nAction << std::endl;
    m_sLogFile.flush();
#endif
    return nErr;

}

int CRTIDome::setRainAction(const int &nAction)
{
    int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    ssCmd << RAIN_ROTATOR_ACTION << nAction << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, RAIN_ROTATOR_ACTION);
    return nErr;

}

int CRTIDome::getPanId(int &nPanId)
{
    int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    ssCmd << PANID << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, PANID);
    if(nErr) {
        return nErr;
    }

    try {
        nPanId = int(std::stol(sResp, NULL, 16));
    }
    catch(const std::exception& e) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getPanId] conversion exception = " << e.what() << std::endl;
        m_sLogFile.flush();
#endif
        nPanId = 0;
    }

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getPanId] nPanId = " << std::uppercase << std::setfill('0') << std::setw(4) << std::hex << nPanId << std::endl;
    m_sLogFile.flush();
#endif

    return nErr;

}

int CRTIDome::setPanId(const int nPanId)
{
    int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    ssCmd << PANID << std::uppercase << std::setfill('0') << std::setw(4) << std::hex << nPanId << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, PANID);
    return nErr;

}

int CRTIDome::getShutterPanId(int &nPanId)
{
    int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    ssCmd << SHUTTER_PANID << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, SHUTTER_PANID);
    if(nErr) {
        return nErr;
    }

    try {
        nPanId = int(std::stol(sResp, NULL, 16));
    }
    catch(const std::exception& e) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getShutterPanId] conversion exception = " << e.what() << std::endl;
        m_sLogFile.flush();
#endif
        nPanId = 0;
    }
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getShutterPanId] nPanId = " << std::uppercase << std::setfill('0') << std::setw(4) << std::hex << nPanId << std::endl;
    m_sLogFile.flush();
#endif

    return nErr;

}
int CRTIDome::isPanIdSet(const int nPanId, bool &bSet)
{
    int nErr = PLUGIN_OK;
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
    int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    ssCmd << RESTORE_MOTOR_DEFAULT << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, RESTORE_MOTOR_DEFAULT);
    if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [restoreDomeMotorSettings] ERROR = " <<nErr << std::endl;
        m_sLogFile.flush();
#endif
    }

    return nErr;
}

int CRTIDome::restoreShutterMotorSettings()
{
    int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;
    int nDummy;
    
    if(!m_bIsConnected)
        return NOT_CONNECTED;

    ssCmd << SHUTTER_RESTORE_MOTOR_DEFAULT << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, SHUTTER_RESTORE_MOTOR_DEFAULT);
    if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [restoreShutterMotorSettings] ERROR = " <<nErr << std::endl;
        m_sLogFile.flush();
#endif
    }
    nErr = getShutterAcceleration(nDummy);
    nErr |= getShutterSpeed(nDummy);
    return nErr;
}

void CRTIDome::enableRainStatusFile(bool bEnable)
{
    m_bSaveRainStatus = bEnable;
}

void CRTIDome::getRainStatusFileName(std::string &fName)
{
    fName.assign(m_sRainStatusfilePath);
}

void CRTIDome::writeRainStatus()
{
    int nStatus;

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [writeRainStatus] m_nIsRaining = " <<(m_nIsRaining==RAINING?"Raining":"Not Raining") << std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [writeRainStatus] m_bSaveRainStatus = " <<(m_bSaveRainStatus?"YES":"NO") << std::endl;
    m_sLogFile.flush();
#endif

    if(m_bSaveRainStatus) {
        getRainSensorStatus(nStatus);
        if(m_nRainStatus != nStatus) {
            m_nRainStatus = nStatus;
            if(m_RainStatusfile.is_open())
                m_RainStatusfile.close();
            try {
                m_RainStatusfile.open(m_sRainStatusfilePath, std::ios::out |std::ios::trunc);
                if(m_RainStatusfile.is_open()) {
                    m_RainStatusfile << "Raining:" << (nStatus == RAINING?"YES":"NO") << std::endl;
                    m_RainStatusfile.close();
                }
            }
            catch(const std::exception& e) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
                m_sLogFile << "["<<getTimeStamp()<<"]"<< " [writeRainStatus] Error writing file = " << e.what() << std::endl;
                m_sLogFile.flush();
#endif
                if(m_RainStatusfile.is_open())
                    m_RainStatusfile.close();
            }
        }
    }
}


int CRTIDome::getMACAddress(std::string &MACAddress)
{
    int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    ssCmd << ETH_MAC_ADDRESS << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, ETH_MAC_ADDRESS);
    if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getMACAddress] ERROR = " <<nErr << std::endl;
        m_sLogFile.flush();
#endif
    }
    MACAddress.assign(sResp);
    return nErr;
}

int CRTIDome::reconfigureNetwork()
{
    int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    ssCmd << ETH_RECONFIG << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, m_bNetworkConnected?0x00:ETH_RECONFIG);

    if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [reconfigureNetwork] ERROR = " <<nErr << std::endl;
        m_sLogFile.flush();
#endif
    }
    return nErr;
}

int CRTIDome::getUseDHCP(bool &bUseDHCP)
{
    int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    ssCmd << IP_DHCP << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, IP_DHCP);
    if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getUseDHCP] ERROR = " <<nErr << std::endl;
        m_sLogFile.flush();
#endif
    }
    bUseDHCP = false;
    if(sResp.size())
        bUseDHCP = (sResp.at(0) == '0'? false: true);
    return nErr;
}

int CRTIDome::setUseDHCP(bool bUseDHCP)
{
    int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    ssCmd << IP_DHCP << (bUseDHCP?"1":"0") << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, IP_DHCP);
    return nErr;
}

int CRTIDome::getIpAddress(std::string &IpAddress)
{
    int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    ssCmd << IP_ADDRESS << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, IP_ADDRESS);
    if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getIpAddress] ERROR = " <<nErr << std::endl;
        m_sLogFile.flush();
#endif
    }
    IpAddress.assign(sResp);
    return nErr;
}

int CRTIDome::setIpAddress(std::string IpAddress)
{
    int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    ssCmd << IP_ADDRESS << IpAddress << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, IP_ADDRESS);
    return nErr;
}

int CRTIDome::getSubnetMask(std::string &subnetMask)
{
    int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    ssCmd << IP_SUBNET << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, IP_SUBNET);
    if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getSubnetMask] ERROR = " <<nErr << std::endl;
        m_sLogFile.flush();
#endif
    }
    subnetMask.assign(sResp);
    return nErr;

}

int CRTIDome::setSubnetMask(std::string subnetMask)
{
    int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    ssCmd << IP_SUBNET << subnetMask << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, IP_SUBNET);
    return nErr;
}

int CRTIDome::getIPGateway(std::string &IpAddress)
{
    int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    ssCmd << IP_GATEWAY << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, IP_GATEWAY);
    if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getIPGateway] ERROR = " <<nErr << std::endl;
        m_sLogFile.flush();
#endif
    }
    IpAddress.assign(sResp);
    return nErr;
}

int CRTIDome::setIPGateway(std::string IpAddress)
{
    int nErr = PLUGIN_OK;
    std::stringstream ssCmd;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    ssCmd << IP_GATEWAY << IpAddress << "#";
    nErr = deviceCommand(ssCmd.str(), sResp, IP_GATEWAY);
    return nErr;
}


int CRTIDome::parseFields(const std::string sResp, std::vector<std::string> &svFields, char cSeparator)
{
    int nErr = PLUGIN_OK;
    std::string sSegment;

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseFields] sResp = " << sResp << std::endl;
    m_sLogFile.flush();
#endif

    if(sResp.size()==0) {
        return MAKE_ERR_CODE(PLUGIN_ID, DriverRootInterface::DT_DOME, ERR_CMDFAILED);
    }

    std::stringstream ssTmp(sResp);

    svFields.clear();
    // split the string into vector elements
    while(std::getline(ssTmp, sSegment, cSeparator))
    {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseFields] sSegment = " << sSegment << std::endl;
        m_sLogFile.flush();
#endif
        svFields.push_back(sSegment);
    }

    if(svFields.size()==0) {
        nErr = MAKE_ERR_CODE(PLUGIN_ID, DriverRootInterface::DT_DOME, ERR_CMDFAILED);
    }
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseFields] Done all good." << std::endl;
    m_sLogFile.flush();
#endif

    return nErr;
}

#ifdef PLUGIN_DEBUG
const std::string CRTIDome::getTimeStamp()
{
    time_t     now = time(0);
    struct tm  tstruct;
    char       buf[80];
    tstruct = *localtime(&now);
    std::strftime(buf, sizeof(buf), "%Y-%m-%d.%X", &tstruct);

    return buf;
}
#endif
