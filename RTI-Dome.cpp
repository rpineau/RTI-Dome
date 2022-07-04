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
    m_sLogFile.open(m_sLogfilePath, std::ios::out |std::ios::trunc);
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

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [Disconnect] m_bIsConnected : " << (m_bIsConnected?"true":"false") << std::endl;
    m_sLogFile.flush();
#endif
}

int CRTIDome::domeCommand(const std::string sCmd, std::string &sResp, char respCmdCode, int nTimeout)
{
    int nErr = PLUGIN_OK;
    unsigned long  ulBytesWrite;
    std::string localResp;

    if(!m_bIsConnected)
        return ERR_COMMNOLINK;

    m_pSerx->purgeTxRx();
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [domeCommand] Sending : " << sCmd << std::endl;
    m_sLogFile.flush();
#endif
    nErr = m_pSerx->writeFile((void *)(sCmd.c_str()), sCmd.size(), ulBytesWrite);
    m_pSerx->flushTx();

    if(nErr){
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [domeCommand] writeFile error : " << nErr << std::endl;
        m_sLogFile.flush();
#endif
        return nErr;
    }

    if (!respCmdCode)
        return nErr;

    // read response
    nErr = readResponse(localResp, nTimeout);
    if(nErr)
        return nErr;

    if(!localResp.size())
        return BAD_CMD_RESPONSE;

    if(localResp.at(0) != respCmdCode)
        nErr = BAD_CMD_RESPONSE;

    sResp = localResp.substr(1, localResp.size());

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [domeCommand] response : " << sResp << std::endl;
    m_sLogFile.flush();
#endif

    return nErr;
}

int CRTIDome::readResponse(std::string &sResp, int nTimeout)
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
    } while (ulTotalBytesRead < SERIAL_BUFFER_SIZE  && *(pszBufPtr-1) != '#');

    if(!ulTotalBytesRead)
        nErr = COMMAND_TIMEOUT; // we didn't get an answer.. so timeout
    else
        *(pszBufPtr-1) = 0; //remove the #

    sResp.assign(pszBuf);
    return nErr;
}


int CRTIDome::getDomeAz(double &dDomeAz)
{
    int nErr = PLUGIN_OK;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(m_bCalibrating)
        return nErr;

    nErr = domeCommand("g#", sResp, 'g');
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
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getDomeAz] convertsion exception = " << e.what() << std::endl;
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
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(m_bCalibrating)
        return nErr;

    nErr = domeCommand("i#", sResp, 'i');
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
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getDomeHomeAz] convertsion exception = " << e.what() << std::endl;
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
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(m_bCalibrating)
        return nErr;

    nErr = domeCommand("l#", sResp, 'l');
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
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getDomeParkAz] convertsion exception = " << e.what() << std::endl;
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

    nErr = domeCommand("M#", sResp, 'M');
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
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getShutterState] convertsion exception = " << e.what() << std::endl;
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
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    nErr = domeCommand("t#", sResp, 't');
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
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getDomeStepPerRev] convertsion exception = " << e.what() << std::endl;
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
    std::string sResp;
    std::stringstream ssTmp;

    m_nNbStepPerRev = nStepPerRev;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    ssTmp << "t" << nStepPerRev <<"#";
    nErr = domeCommand(ssTmp.str(), sResp, 'i');
    return nErr;

}

#pragma mark - TODO : Convert sscanf
int CRTIDome::getBatteryLevels(double &domeVolts, double &dDomeCutOff, double &dShutterVolts, double &dShutterCutOff)
{
    int nErr = PLUGIN_OK;
    std::string sResp;
    std::vector<std::string> voltsFields;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(m_bCalibrating)
        return nErr;
    // Dome
    nErr = domeCommand("k#", sResp, 'k');
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
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getBatteryLevels] convertsion exception = " << e.what() << std::endl;
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
        nErr = domeCommand("K#", sResp, 'K');
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
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getBatteryLevels] convertsion exception = " << e.what() << std::endl;
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
    std::string sResp;
    std::stringstream ssTmp;
    int nRotCutOff, nShutCutOff;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(m_bCalibrating)
        return nErr;

    nRotCutOff = dDomeCutOff * 100.0;
    nShutCutOff = dShutterCutOff * 100.0;

    // Dome
    ssTmp << "k" << nRotCutOff <<"#";
    nErr = domeCommand(ssTmp.str(), sResp, 'k');
    if(nErr) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [setBatteryCutOff] dDomeCutOff ERROR = " << sResp << std::endl;
        m_sLogFile.flush();
#endif
        return nErr;
    }

    if(m_bShutterPresent) {
        // Shutter
        std::stringstream().swap(ssTmp);
        ssTmp << "k" << nShutCutOff <<"#";
        nErr = domeCommand(ssTmp.str(), sResp, 'K');
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
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    nErr = domeCommand("m#", sResp, 'm');
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
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [isDomeMoving] convertsion exception = " << e.what() << std::endl;
        m_sLogFile.flush();
#endif
        nTmp = MOVE_NONE;
    }
#ifdef PLUGIN_DEBUG
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [isDomeMoving] nTmp : " << nTmp << std::endl;
    m_sLogFile.flush();
#endif
    if(nTmp != MOVE_NONE)
        bIsMoving = true;

#ifdef PLUGIN_DEBUG
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
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    nErr = domeCommand("z#", sResp, 'z');
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
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [isDomeAtHome] convertsion exception = " << e.what() << std::endl;
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
    std::stringstream ssTmp;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    m_dCurrentAzPosition = dAz;
    ssTmp << "s" << std::fixed << std::setprecision(2) << dAz << "#";
    nErr = domeCommand(ssTmp.str(), sResp, 's');
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
    std::stringstream ssTmp;
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

    ssTmp << "g" << std::fixed << std::setprecision(2) << dNewAz << "#";
    nErr = domeCommand(ssTmp.str(), sResp, 'g');
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
    std::string sResp;
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
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [openShutter] m_bShutterPresent : " << (m_bShutterPresent?"True":"False") << std::endl;
    m_sLogFile.flush();
#endif
    if(!m_bShutterPresent) {
        return SB_OK;
    }

    getBatteryLevels(domeVolts, dDomeCutOff, dShutterVolts, dShutterCutOff);
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [openShutter] Opening shutter" << std::endl;
    m_sLogFile.flush();
#endif

    nErr = domeCommand("O#", sResp, 'O');
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
    std::string sResp;
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
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [closeShutter] m_bShutterPresent = " << (m_bShutterPresent?"Yes":"No") << std::endl;
    m_sLogFile.flush();
#endif

    if(!m_bShutterPresent) {
        return SB_OK;
    }

    getBatteryLevels(domeVolts, dDomeCutOff, dShutterVolts, dShutterCutOff);

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [closeShutter] Closing shutter" << std::endl;
    m_sLogFile.flush();
#endif

    nErr = domeCommand("C#", sResp, 'C');
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
    std::string sResp;
    std::vector<std::string> firmwareFields;
    std::vector<std::string> versionFields;
    std::string strVersion;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(m_bCalibrating)
        return SB_OK;

    nErr = domeCommand("v#", sResp, 'v');
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
                m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getFirmwareVersion] convertsion exception = " << e.what() << std::endl;
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
    std::string sResp;
    std::vector<std::string> firmwareFields;
    std::vector<std::string> versionFields;
    std::string strVersion;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(m_bCalibrating)
        return SB_OK;

    nErr = domeCommand("V#", sResp, 'V');
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
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getShutterFirmwareVersion] convertsion exception = " << e.what() << std::endl;
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
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(m_bCalibrating) {
        return SB_OK;
    }
    else if(isDomeAtHome()){
            return PLUGIN_OK;
    }
#ifdef PLUGIN_DEBUG
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [goHome]" << std::endl;
    m_sLogFile.flush();
#endif

    m_nHomingTries = 0;
    nErr = domeCommand("h#", sResp, 'h');
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
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;


    nErr = domeCommand("c#", sResp, 'c');
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


bool CRTIDome::checkBoundaries(double dGotoAz, double dDomeAz)
{
    double highMark;
    double lowMark;
    double roundedGotoAz;

    // we need to test "large" depending on the heading error and movement coasting
    highMark = ceil(dDomeAz)+2;
    lowMark = ceil(dDomeAz)-2;
    roundedGotoAz = ceil(dGotoAz);

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
        bComplete = true;
        return SB_OK;
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
        bComplete = true;
        return SB_OK;
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
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    m_bParked = false;
    m_bCalibrating = false;
    m_bParking = false;
    m_bUnParking = false;
    m_nGotoTries = 1;   // prevents the goto retry
    m_nHomingTries = 1; // prevents the find home retry

    nErr = domeCommand("a#", sResp, 'a');

    getDomeAz(m_dGotoAz);

    return nErr;
}

int CRTIDome::sendShutterHello()
{
    int nErr = PLUGIN_OK;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    nErr = domeCommand("H#", sResp, 0x00); // we don't want the response
    return nErr;
}

int CRTIDome::getShutterPresent(bool &bShutterPresent)
{
    int nErr = PLUGIN_OK;
    std::string sResp;

    nErr = domeCommand("o#", sResp, 'o');
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
    std::string sResp;
    std::stringstream ssTmp;
    m_dHomeAz = dAz;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    ssTmp << "i" << std::fixed << std::setprecision(2) << dAz << "#";
    nErr = domeCommand(ssTmp.str(), sResp, 'i');
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
    std::string sResp;
    std::stringstream ssTmp;

    m_dParkAz = dAz;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    ssTmp << "l" << std::fixed << std::setprecision(2) << dAz << "#";
    nErr = domeCommand(ssTmp.str(), sResp, 'l');
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
    std::string sResp;

    bNormal = true;
    nErr = domeCommand("y#", sResp, 'y');
    if(nErr) {
        return nErr;
    }
    try {
        bNormal = std::stoi(sResp) ? false:true;
    }
    catch(const std::exception& e) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getDefaultDir] convertsion exception = " << e.what() << std::endl;
        m_sLogFile.flush();
#endif
        bNormal = true;
    }

#ifdef PLUGIN_DEBUG
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getDefaultDir] bNormal = " << (bNormal?"True":"False") << std::endl;
    m_sLogFile.flush();
#endif

    return nErr;
}

int CRTIDome::setDefaultDir(bool bNormal)
{
    int nErr = PLUGIN_OK;
    std::string sResp;
    std::stringstream ssTmp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    ssTmp << "y" << (bNormal?"0":"1") << "#";

#ifdef PLUGIN_DEBUG
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [setDefaultDir] bNormal = " << (bNormal?"True":"False") << std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [setDefaultDir] ssTmp = " << ssTmp.str() << std::endl;
    m_sLogFile.flush();
#endif

    nErr = domeCommand(ssTmp.str(), sResp, 'y');
    return nErr;

}

int CRTIDome::getRainSensorStatus(int &nStatus)
{
    int nErr = PLUGIN_OK;
    std::string sResp;

    nStatus = NOT_RAINING;
    nErr = domeCommand("F#", sResp, 'F');
    if(nErr) {
        return nErr;
    }

    try {
        nStatus = std::stoi(sResp) ? false:true;
    }
    catch(const std::exception& e) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getRainSensorStatus] convertsion exception = " << e.what() << std::endl;
        m_sLogFile.flush();
#endif
        nStatus = false;
    }

#ifdef PLUGIN_DEBUG
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getRainSensorStatus] nStatus = " << (nStatus?"NOT RAINING":"RAINING") << std::endl;
    m_sLogFile.flush();
#endif

    m_nIsRaining = nStatus;
    return nErr;
}

int CRTIDome::getRotationSpeed(int &nSpeed)
{
    int nErr = PLUGIN_OK;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    nErr = domeCommand("r#", sResp, 'r');
    if(nErr) {
        return nErr;
    }

    try{
        nSpeed = std::stoi(sResp);
    }
    catch(const std::exception& e) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getRotationSpeed] convertsion exception = " << e.what() << std::endl;
        m_sLogFile.flush();
#endif
        nSpeed = 0;
    }

#ifdef PLUGIN_DEBUG
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getRotationSpeed] nSpeed = " << nSpeed << std::endl;
    m_sLogFile.flush();
#endif

    return nErr;
}

int CRTIDome::setRotationSpeed(int nSpeed)
{
    int nErr = PLUGIN_OK;
    std::stringstream ssTmp;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    ssTmp << "r" << nSpeed << "#";
    nErr = domeCommand(ssTmp.str(), sResp, 'r');
    return nErr;
}


int CRTIDome::getRotationAcceleration(int &nAcceleration)
{
    int nErr = PLUGIN_OK;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    nErr = domeCommand("e#", sResp, 'e');
    if(nErr) {
        return nErr;
    }

    try {
        nAcceleration = std::stoi(sResp);
    }
    catch(const std::exception& e) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getRotationAcceleration] convertsion exception = " << e.what() << std::endl;
        m_sLogFile.flush();
#endif
        nAcceleration = 0;
    }
#ifdef PLUGIN_DEBUG
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getRotationAcceleration] nAcceleration = " << nAcceleration << std::endl;
    m_sLogFile.flush();
#endif

    return nErr;
}

int CRTIDome::setRotationAcceleration(int nAcceleration)
{
    int nErr = PLUGIN_OK;
    std::stringstream ssTmp;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    ssTmp << "e" << nAcceleration << "#";
    nErr = domeCommand(ssTmp.str(), sResp, 'e');

    return nErr;
}

int CRTIDome::getShutterSpeed(int &nSpeed)
{
    int nErr = PLUGIN_OK;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(!m_bShutterPresent) {
        nSpeed = 0;
        return SB_OK;
    }

    nErr = domeCommand("R#", sResp, 'R');
    if(nErr) {
        return nErr;
    }

    try {
        nSpeed = std::stoi(sResp);
    }
    catch(const std::exception& e) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getShutterSpeed] convertsion exception = " << e.what() << std::endl;
        m_sLogFile.flush();
#endif
        nSpeed = 0;
    }
#ifdef PLUGIN_DEBUG
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getShutterSpeed] nSpeed = " << nSpeed << std::endl;
    m_sLogFile.flush();
#endif

    return nErr;
}

int CRTIDome::setShutterSpeed(int nSpeed)
{
    int nErr = PLUGIN_OK;
    std::stringstream ssTmp;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(!m_bShutterPresent) {
        return SB_OK;
    }

    ssTmp << "R" << nSpeed << "#";
    nErr = domeCommand(ssTmp.str(), sResp, 'R');

    return nErr;
}

int CRTIDome::getShutterAcceleration(int &nAcceleration)
{
    int nErr = PLUGIN_OK;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(!m_bShutterPresent) {
        nAcceleration = 0;
        return SB_OK;
    }

    nErr = domeCommand("E#", sResp, 'E');
    if(nErr) {
        return nErr;
    }

    try {
        nAcceleration = std::stoi(sResp);
    }
    catch(const std::exception& e) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getShutterAcceleration] convertsion exception = " << e.what() << std::endl;
        m_sLogFile.flush();
#endif
        nAcceleration = 0;
    }
#ifdef PLUGIN_DEBUG
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getShutterAcceleration] nAcceleration = " << nAcceleration << std::endl;
    m_sLogFile.flush();
#endif
    return nErr;
}

int CRTIDome::setShutterAcceleration(int nAcceleration)
{
    int nErr = PLUGIN_OK;
    std::stringstream ssTmp;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(!m_bShutterPresent) {
        return SB_OK;
    }

    ssTmp << "E" << nAcceleration << "#";
    nErr = domeCommand(ssTmp.str(), sResp, 'E');
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
    std::string sResp;

	if(!m_bIsConnected)
		return NOT_CONNECTED;

    if(!m_bShutterPresent) {
        nValue = 0;
        return SB_OK;
    }

    nErr = domeCommand("I#", sResp, 'I');
	if(nErr) {
		return nErr;
	}

    try {
        nValue = std::stoi(sResp)/1000; // value is in ms
    }
    catch(const std::exception& e) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getSutterWatchdogTimerValue] convertsion exception = " << e.what() << std::endl;
        m_sLogFile.flush();
#endif
        nValue = 0;
    }

#ifdef PLUGIN_DEBUG
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getSutterWatchdogTimerValue] nValue = " << nValue << std::endl;
    m_sLogFile.flush();
#endif
	return nErr;
}

int	CRTIDome::setSutterWatchdogTimerValue(const int &nValue)
{
	int nErr = PLUGIN_OK;
    std::stringstream ssTmp;
    std::string sResp;

	if(!m_bIsConnected)
		return NOT_CONNECTED;

    if(!m_bShutterPresent) {
        return SB_OK;
    }

    ssTmp << "I" << (nValue * 1000) << "#";
    nErr = domeCommand(ssTmp.str(), sResp, 'I');
	return nErr;
}

int CRTIDome::getRainAction(int &nAction)
{
    int nErr = PLUGIN_OK;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    nErr = domeCommand("n#", sResp, 'n');
    if(nErr) {
        return nErr;
    }

    try {
        nAction = std::stoi(sResp);
    }
    catch(const std::exception& e) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getRainAction] convertsion exception = " << e.what() << std::endl;
        m_sLogFile.flush();
#endif
        nAction = 0;
    }

#ifdef PLUGIN_DEBUG
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getRainTimerValue] nAction = " << nAction << std::endl;
    m_sLogFile.flush();
#endif
    return nErr;

}

int CRTIDome::setRainAction(const int &nAction)
{
    int nErr = PLUGIN_OK;
    std::stringstream ssTmp;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    ssTmp << "n" << nAction << "#";
    nErr = domeCommand(ssTmp.str(), sResp, 'n');
    return nErr;

}

int CRTIDome::getPanId(int &nPanId)
{
    int nErr = PLUGIN_OK;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    nErr = domeCommand("q#", sResp, 'q');
    if(nErr) {
        return nErr;
    }

    try {
        nPanId = int(std::stol(sResp, NULL, 16));
    }
    catch(const std::exception& e) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getPanId] convertsion exception = " << e.what() << std::endl;
        m_sLogFile.flush();
#endif
        nPanId = 0;
    }

#ifdef PLUGIN_DEBUG
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getPanId] nPanId = " << std::uppercase << std::setfill('0') << std::setw(4) << std::hex << nPanId << std::endl;
    m_sLogFile.flush();
#endif

    return nErr;

}

int CRTIDome::setPanId(const int nPanId)
{
    int nErr = PLUGIN_OK;
    std::stringstream ssTmp;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    ssTmp << "q" << std::uppercase << std::setfill('0') << std::setw(4) << std::hex << nPanId << "#";
    nErr = domeCommand(ssTmp.str(), sResp, 'q');
    return nErr;

}

int CRTIDome::getShutterPanId(int &nPanId)
{
    int nErr = PLUGIN_OK;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    nErr = domeCommand("Q#", sResp, 'Q');
    if(nErr) {
        return nErr;
    }

    try {
        nPanId = int(std::stol(sResp, NULL, 16));
    }
    catch(const std::exception& e) {
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getShutterPanId] convertsion exception = " << e.what() << std::endl;
        m_sLogFile.flush();
#endif
        nPanId = 0;
    }
#ifdef PLUGIN_DEBUG
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
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    nErr = domeCommand("d#", sResp, 'd');
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
    std::string sResp;
    int nDummy;
    
    if(!m_bIsConnected)
        return NOT_CONNECTED;

    nErr = domeCommand("D#", sResp, 'D');
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

#ifdef PLUGIN_DEBUG
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
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    nErr = domeCommand("f#", sResp, 'f');
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
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    if(m_bNetworkConnected) {
        nErr = domeCommand("b#", sResp, 0x00); // we won't get an answer as reconfiguring the network will disconnect us.
    }
    else {
        nErr = domeCommand("b#", sResp, 'b');
    }
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
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    nErr = domeCommand("w#", sResp, 'w');
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
    std::stringstream ssTmp;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    ssTmp << "w" << (bUseDHCP?"1":"0") << "#";
    nErr = domeCommand(ssTmp.str(), sResp, 'w');
    return nErr;
}

int CRTIDome::getIpAddress(std::string &IpAddress)
{
    int nErr = PLUGIN_OK;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    nErr = domeCommand("j#", sResp, 'j');
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
    std::stringstream ssTmp;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    ssTmp << "j" << IpAddress << "#";
    nErr = domeCommand(ssTmp.str(), sResp, 'j');
    return nErr;
}

int CRTIDome::getSubnetMask(std::string &subnetMask)
{
    int nErr = PLUGIN_OK;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    nErr = domeCommand("p#", sResp, 'p');
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
    std::stringstream ssTmp;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    ssTmp << "p" << subnetMask << "#";
    nErr = domeCommand(ssTmp.str(), sResp, 'p');
    return nErr;
}

int CRTIDome::getIPGateway(std::string &IpAddress)
{
    int nErr = PLUGIN_OK;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    nErr = domeCommand("u#", sResp, 'u');
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
    std::stringstream ssTmp;
    std::string sResp;

    if(!m_bIsConnected)
        return NOT_CONNECTED;

    ssTmp << "u" << IpAddress << "#";
    nErr = domeCommand(ssTmp.str(), sResp, 'u');
    return nErr;
}


int CRTIDome::parseFields(const std::string sResp, std::vector<std::string> &svFields, char cSeparator)
{
    int nErr = PLUGIN_OK;
    std::string sSegment;

#ifdef PLUGIN_DEBUG
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
#ifdef PLUGIN_DEBUG
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
