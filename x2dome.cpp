#include "x2dome.h"


X2Dome::X2Dome(const char* pszSelection,
							 const int& nISIndex,
					SerXInterface*						pSerX,
					TheSkyXFacadeForDriversInterface*	pTheSkyXForMounts,
					SleeperInterface*					pSleeper,
					BasicIniUtilInterface*			pIniUtil,
					LoggerInterface*					pLogger,
					MutexInterface*						pIOMutex,
					TickCountInterface*					pTickCount)
{

    m_nPrivateISIndex				= nISIndex;
	m_pSerX							= pSerX;
	m_pTheSkyXForMounts				= pTheSkyXForMounts;
	m_pSleeper						= pSleeper;
	m_pIniUtil						= pIniUtil;
	m_pLogger						= pLogger;
	m_pIOMutex						= pIOMutex;
	m_pTickCount					= pTickCount;

	m_bLinked = false;
    m_bHomingDome = false;
    m_bCalibratingDome = false;
    m_nBattRequest = 0;
    m_bSettingPanID = false;
    m_bHasShutterControl = false;
    
    m_RTIDome.setSerxPointer(pSerX);
    m_RTIDome.setSleeprPinter(pSleeper);

    if (m_pIniUtil)
    {
        m_RTIDome.setHomeAz( m_pIniUtil->readDouble(PARENT_KEY, CHILD_KEY_HOME_AZ, 0) );
        m_RTIDome.setParkAz( m_pIniUtil->readDouble(PARENT_KEY, CHILD_KEY_PARK_AZ, 0) );
        m_bLogRainStatus = m_pIniUtil->readInt(PARENT_KEY, CHILD_KEY_LOG_RAIN_STATUS, false);

        m_bHomeOnPark = m_pIniUtil->readInt(PARENT_KEY, CHILD_KEY_HOME_ON_PARK, false);
        m_bHomeOnUnpark = m_pIniUtil->readInt(PARENT_KEY, CHILD_KEY_HOME_ON_UNPARK, false);
        m_RTIDome.setHomeOnPark(m_bHomeOnPark);
        m_RTIDome.setHomeOnUnpark(m_bHomeOnUnpark);
        m_RTIDome.enableRainStatusFile(m_bLogRainStatus);
    }
}


X2Dome::~X2Dome()
{
	if (m_pSerX)
		delete m_pSerX;
	if (m_pTheSkyXForMounts)
		delete m_pTheSkyXForMounts;
	if (m_pSleeper)
		delete m_pSleeper;
	if (m_pIniUtil)
		delete m_pIniUtil;
	if (m_pLogger)
		delete m_pLogger;
	if (m_pIOMutex)
		delete m_pIOMutex;
	if (m_pTickCount)
		delete m_pTickCount;

}


int X2Dome::establishLink(void)
{
    int nErr;
    char szPort[SERIAL_BUFFER_SIZE];

    X2MutexLocker ml(GetMutex());

    // get serial port device name
    portNameOnToCharPtr(szPort,SERIAL_BUFFER_SIZE);
    nErr = m_RTIDome.Connect(szPort);
    if(nErr) {
        m_bLinked = false;
        // nErr = ERR_COMMOPENING;
    }
    else
        m_bLinked = true;

    m_RTIDome.getShutterPresent(m_bHasShutterControl);
	return nErr;
}

int X2Dome::terminateLink(void)
{
    X2MutexLocker ml(GetMutex());

    m_RTIDome.Disconnect();
	m_bLinked = false;

    return SB_OK;
}

 bool X2Dome::isLinked(void) const
{
    return m_bLinked;
}


int X2Dome::queryAbstraction(const char* pszName, void** ppVal)
{
    *ppVal = NULL;

    if (!strcmp(pszName, LoggerInterface_Name))
        *ppVal = GetLogger();
    else if (!strcmp(pszName, ModalSettingsDialogInterface_Name))
        *ppVal = dynamic_cast<ModalSettingsDialogInterface*>(this);
    else if (!strcmp(pszName, X2GUIEventInterface_Name))
        *ppVal = dynamic_cast<X2GUIEventInterface*>(this);
    else if (!strcmp(pszName, SerialPortParams2Interface_Name))
        *ppVal = dynamic_cast<SerialPortParams2Interface*>(this);

    return SB_OK;
}

#pragma mark - UI binding

int X2Dome::execModalSettingsDialog()
{
    int nErr = SB_OK;
    X2ModalUIUtil uiutil(this, GetTheSkyXFacadeForDrivers());
    X2GUIInterface*					ui = uiutil.X2UI();
    X2GUIExchangeInterface*			dx = NULL;//Comes after ui is loaded
    bool bPressedOK = false;
    char szTmpBuf[SERIAL_BUFFER_SIZE];
    std::string fName;
    double dHomeAz;
    double dParkAz;
    double dDomeBattery, dDomeCutOff;
    double dShutterBattery, dShutterCutOff;
    bool nReverseDir;
    int n_nbStepPerRev;
    int nRainSensorStatus = NOT_RAINING;
    int nRSpeed;
    int nRAcc;
    int nSSpeed;
    int nSAcc;
	int nWatchdog;
    int nRainTimer;
    int nRainAction;
    double  batRotCutOff;
    double  batShutCutOff;
    
    if (NULL == ui)
        return ERR_POINTER;

    if ((nErr = ui->loadUserInterface("RTI-Dome.ui", deviceType(), m_nPrivateISIndex)))
        return nErr;

    if (NULL == (dx = uiutil.X2DX()))
        return ERR_POINTER;

    X2MutexLocker ml(GetMutex());
    m_RTIDome.getShutterPresent(m_bHasShutterControl);

    memset(szTmpBuf,0,SERIAL_BUFFER_SIZE);
    // set controls state depending on the connection state
    if(m_bHasShutterControl) {
        dx->setChecked("hasShutterCtrl",true);
    }
    else {
        dx->setChecked("hasShutterCtrl",false);
    }

    if(m_bHomeOnPark) {
        dx->setChecked("homeOnPark",true);
    }
    else {
        dx->setChecked("homeOnPark",false);
    }

    if(m_bHomeOnUnpark) {
        dx->setChecked("homeOnUnpark",true);
    }
    else {
        dx->setChecked("homeOnUnpark",false);
    }

    if(m_bLogRainStatus) {
        dx->setChecked("checkBox",true);
        m_RTIDome.getRainStatusFileName(fName);
        dx->setPropertyString("filePath","text", fName.c_str());
    }
    else {
        dx->setChecked("checkBox",false);
        dx->setPropertyString("filePath","text", "");
    }

    if(m_bLinked) {
        if(m_bHasShutterControl)
            m_RTIDome.sendShutterHello();   // refresh values.
        dx->setEnabled("homePosition",true);
        dx->setEnabled("parkPosition",true);
        dx->setEnabled("needReverse",true);
        nErr = m_RTIDome.getDefaultDir(nReverseDir);
        if(nReverseDir)
            dx->setChecked("needReverse",false);
        else
            dx->setChecked("needReverse",true);

        // read values from dome controller
        dx->setEnabled("ticksPerRev",true);
        n_nbStepPerRev = m_RTIDome.getNbTicksPerRev();
        dx->setPropertyInt("ticksPerRev","value", n_nbStepPerRev);

        dx->setEnabled("rotationSpeed",true);
        m_RTIDome.getRotationSpeed(nRSpeed);
        dx->setPropertyInt("rotationSpeed","value", nRSpeed);

        dx->setEnabled("rotationAcceletation",true);
        m_RTIDome.getRotationAcceleration(nRAcc);
        dx->setPropertyInt("rotationAcceletation","value", nRAcc);

        dx->setEnabled("pushButton_3", true);

        if(m_bHasShutterControl) {
            dx->setEnabled("shutterSpeed",true);
            nErr = m_RTIDome.getShutterSpeed(nSSpeed);
            dx->setPropertyInt("shutterSpeed","value", nSSpeed);

            dx->setEnabled("shutterAcceleration",true);
            m_RTIDome.getShutterAcceleration(nSAcc);
            dx->setPropertyInt("shutterAcceleration","value", nSAcc);

            dx->setEnabled("pushButton_4", true);

            dx->setEnabled("shutterWatchdog",true);
            m_RTIDome.getSutterWatchdogTimerValue(nWatchdog);
            dx->setPropertyInt("shutterWatchdog", "value", nWatchdog);

            dx->setEnabled("lowShutBatCutOff",true);
        } else {
            dx->setEnabled("shutterSpeed",false);
            dx->setPropertyInt("shutterSpeed","value",0);
            dx->setEnabled("shutterAcceleration",false);
            dx->setPropertyInt("shutterAcceleration","value",0);
            dx->setEnabled("shutterWatchdog",false);
            dx->setPropertyInt("shutterWatchdog","value",0);
            dx->setEnabled("pushButton_4", false);
            dx->setPropertyInt("shutterWatchdog", "value", 0);
            dx->setEnabled("lowShutBatCutOff",false);
        }

        dx->setEnabled("lowRotBatCutOff",true);

        dx->setEnabled("rainCheckInterval",true);
        m_RTIDome.getRainTimerValue(nRainTimer);
        dx->setPropertyInt("rainCheckInterval", "value", nRainTimer);


        if(m_bHasShutterControl) {
            dx->setText("shutterPresent", "Shutter present");
        }
        else {
            dx->setText("shutterPresent", "No Shutter detected");
        }
        // panID
        dx->setEnabled("panID", true);
        dx->setEnabled("pushButton_2", true);
        nErr = m_RTIDome.getPanId(m_nPanId);
        if(nErr)
            m_nPanId = 0;
        dx->setPropertyInt("panID", "value", m_nPanId);

        m_RTIDome.getBatteryLevels(dDomeBattery, dDomeCutOff, dShutterBattery, dShutterCutOff);
        dx->setPropertyDouble("lowRotBatCutOff","value", dDomeCutOff);

        snprintf(szTmpBuf,16,"%2.2f V",dDomeBattery);
        dx->setPropertyString("domeBatteryLevel","text", szTmpBuf);

        if(m_bHasShutterControl) {
            dx->setPropertyDouble("lowShutBatCutOff","value", dShutterCutOff);
            snprintf(szTmpBuf,16,"%2.2f V",dShutterCutOff);
            dx->setPropertyString("lowShutBatCutOff","text", szTmpBuf);

            if(dShutterBattery>=0.0f)
                snprintf(szTmpBuf,16,"%2.2f V",dShutterBattery);
            else
                snprintf(szTmpBuf,16,"--");
            dx->setPropertyString("shutterBatteryLevel","text", szTmpBuf);
        }
        else {
            dx->setPropertyDouble("lowShutBatCutOff","value", 0);
            dx->setPropertyString("shutterBatteryLevel","text", "--");
        }

        m_RTIDome.getRainAction(nRainAction);
        dx->setCurrentIndex("comboBox", nRainAction);
        
        nErr = m_RTIDome.getRainSensorStatus(nRainSensorStatus);
        if(nErr)
            dx->setPropertyString("rainStatus","text", "--");
        else {
            snprintf(szTmpBuf, 16, nRainSensorStatus==NOT_RAINING ? "Not raining" : "Raining");
            dx->setPropertyString("rainStatus","text", szTmpBuf);
        }

        dx->setEnabled("pushButton",true);
    }
    else {
        dx->setEnabled("homePosition", false);
        dx->setEnabled("parkPosition", false);
        dx->setEnabled("needReverse", false);
        dx->setEnabled("ticksPerRev", false);
        dx->setEnabled("rotationSpeed", false);
        dx->setEnabled("rotationAcceletation", false);
        dx->setEnabled("shutterSpeed", false);
        dx->setEnabled("shutterAcceleration", false);
		dx->setEnabled("shutterWatchdog", false);
        dx->setEnabled("rainCheckInterval", false);
        dx->setEnabled("lowRotBatCutOff", false);
        dx->setEnabled("lowShutBatCutOff", false);
        dx->setEnabled("comboBox", false);
        dx->setPropertyString("domeBatteryLevel", "text", "--");
        dx->setPropertyString("shutterBatteryLevel", "text", "--");
        dx->setEnabled("panID", false);
        dx->setEnabled("pushButton_2", false);
        dx->setEnabled("pushButton", false);
        dx->setEnabled("pushButton_3", false);
        dx->setEnabled("pushButton_4", false);
        dx->setPropertyString("domePointingError", "text", "--");
        dx->setPropertyString("rainStatus","text", "--");
    }
    dx->setPropertyDouble("homePosition","value", m_RTIDome.getHomeAz());
    dx->setPropertyDouble("parkPosition","value", m_RTIDome.getParkAz());


    m_bHomingDome = false;
    m_nBattRequest = 0;


    //Display the user interface
    if ((nErr = ui->exec(bPressedOK)))
        return nErr;

    //Retreive values from the user interface
    if (bPressedOK) {
        dx->propertyInt("ticksPerRev", "value", n_nbStepPerRev);
        dx->propertyDouble("homePosition", "value", dHomeAz);
        dx->propertyDouble("parkPosition", "value", dParkAz);
        dx->propertyInt("rotationSpeed", "value", nRSpeed);
        dx->propertyInt("rotationAcceletation", "value", nRAcc);
        dx->propertyInt("shutterSpeed", "value", nSSpeed);
        dx->propertyInt("shutterAcceleration", "value", nSAcc);
		dx->propertyInt("shutterWatchdog", "value", nWatchdog);
        dx->propertyInt("rainCheckInterval", "value", nRainTimer);
        dx->propertyDouble("lowRotBatCutOff", "value", batRotCutOff);
        dx->propertyDouble("lowShutBatCutOff", "value", batShutCutOff);
        nRainAction = dx->currentIndex("comboBox");
        m_bHomeOnPark = dx->isChecked("homeOnPark");
        m_RTIDome.setHomeOnPark(m_bHomeOnPark);
        m_bHomeOnUnpark = dx->isChecked("homeOnUnpark");
        m_RTIDome.setHomeOnUnpark(m_bHomeOnUnpark);
        nReverseDir = dx->isChecked("needReverse");
        m_bLogRainStatus = dx->isChecked("checkBox");
        m_RTIDome.enableRainStatusFile(m_bLogRainStatus);

        if(m_bLinked) {
            m_RTIDome.setDefaultDir(!nReverseDir);
            m_RTIDome.setHomeAz(dHomeAz);
            m_RTIDome.setParkAz(dParkAz);
            m_RTIDome.setNbTicksPerRev(n_nbStepPerRev);
            m_RTIDome.setRotationSpeed(nRSpeed);
            m_RTIDome.setRotationAcceleration(nRAcc);
            m_RTIDome.setRainTimerValue(nRainTimer);
			m_RTIDome.setBatteryCutOff(batRotCutOff, batShutCutOff);
            m_RTIDome.setRainAction(nRainAction);
			if(m_bHasShutterControl) {
				m_RTIDome.setShutterSpeed(nSSpeed);
				m_RTIDome.setShutterAcceleration(nSAcc);
				m_RTIDome.setSutterWatchdogTimerValue(nWatchdog);
				m_RTIDome.sendShutterHello();
			}
        }

        // save the values to persistent storage
        nErr |= m_pIniUtil->writeDouble(PARENT_KEY, CHILD_KEY_HOME_AZ, dHomeAz);
        nErr |= m_pIniUtil->writeDouble(PARENT_KEY, CHILD_KEY_PARK_AZ, dParkAz);
        nErr |= m_pIniUtil->writeInt(PARENT_KEY, CHILD_KEY_SHUTTER_CONTROL, m_bHasShutterControl);
        nErr |= m_pIniUtil->writeInt(PARENT_KEY, CHILD_KEY_HOME_ON_PARK, m_bHomeOnPark);
        nErr |= m_pIniUtil->writeInt(PARENT_KEY, CHILD_KEY_HOME_ON_UNPARK, m_bHomeOnUnpark);
        nErr |= m_pIniUtil->writeInt(PARENT_KEY, CHILD_KEY_LOG_RAIN_STATUS, m_bLogRainStatus);
    }
    return nErr;

}

void X2Dome::uiEvent(X2GUIExchangeInterface* uiex, const char* pszEvent)
{
    bool bComplete = false;
    int nErr;
    double dDomeBattery, dDomeCutOff;
    double dShutterBattery, dShutterCutOff;
    char szTmpBuf[SERIAL_BUFFER_SIZE];
    char szErrorMessage[LOG_BUFFER_SIZE];
    std::string fName;
    int nRainSensorStatus = NOT_RAINING;
    bool bShutterPresent;
    int nPanId;
    int nSpeed;
    int nAcc;
    int n_nbStepPerRev;
    int nWatchdog;

    if (!strcmp(pszEvent, "on_pushButtonCancel_clicked") && (m_bCalibratingDome || m_bHomingDome))
        m_RTIDome.abortCurrentCommand();

    if (!strcmp(pszEvent, "on_timer"))
    {
        bShutterPresent = uiex->isChecked("hasShutterCtrl");
        m_RTIDome.getShutterPresent(bShutterPresent);

        if(bShutterPresent != m_bHasShutterControl) {
            m_bHasShutterControl = bShutterPresent;
            if(m_bHasShutterControl && m_bLinked) {
                uiex->setText("shutterPresent", "Shutter present");
                uiex->setEnabled("shutterSpeed",true);
                m_RTIDome.getShutterSpeed(nSpeed);
                uiex->setPropertyInt("shutterSpeed","value", nSpeed);

                uiex->setEnabled("shutterAcceleration",true);
                m_RTIDome.getShutterAcceleration(nAcc);
                uiex->setPropertyInt("shutterAcceleration","value", nAcc);

                uiex->setEnabled("shutterWatchdog",true);
                m_RTIDome.getSutterWatchdogTimerValue(nWatchdog);
                uiex->setPropertyInt("shutterWatchdog", "value", nWatchdog);
            }
            else {
                uiex->setText("shutterPresent", "No Shutter detected");
                uiex->setPropertyInt("shutterSpeed","value", 0);
                uiex->setPropertyInt("shutterAcceleration","value", 0);
                uiex->setPropertyInt("shutterWatchdog", "value", 0);
                uiex->setEnabled("shutterSpeed",false);
                uiex->setEnabled("shutterAcceleration",false);
                uiex->setEnabled("shutterWatchdog",false);
                uiex->setPropertyString("shutterBatteryLevel","text", "--");

            }

        }
        if(m_bLinked) {
            // are we going to Home position to calibrate ?
            if(m_bHomingDome) {
                // are we home ?
                bComplete = false;
                nErr = m_RTIDome.isFindHomeComplete(bComplete);
                if(nErr) {
                    snprintf(szErrorMessage, LOG_BUFFER_SIZE, "Error homing dome while calibrating dome : Error %d", nErr);
                    uiex->messageBox("RTI-Dome Calibrate", szErrorMessage);
                    m_bHomingDome = false;
                    m_bCalibratingDome = false;
					// enable buttons
					uiex->setEnabled("pushButton",true);
					uiex->setEnabled("pushButtonOK",true);
                    return;
                }
                if(bComplete) {
                    m_bHomingDome = false;
                    m_bCalibratingDome = true;
                    m_RTIDome.calibrate();
                    return;
                }
            }

           if(m_bCalibratingDome) {
                // are we still calibrating ?
                bComplete = false;
                nErr = m_RTIDome.isCalibratingComplete(bComplete);
                if(nErr) {
                    uiex->setEnabled("pushButtonOK",true);
					uiex->setEnabled("pushButtonCancel", true);
                    snprintf(szErrorMessage, LOG_BUFFER_SIZE, "Error calibrating dome : Error %d", nErr);
                    uiex->messageBox("RTI-Dome Calibrate", szErrorMessage);
                    m_bHomingDome = false;
                    m_bCalibratingDome = false;
                    return;
                }

                if(!bComplete) {
                    return;
                }

                // enable buttons
                uiex->setEnabled("pushButtonOK",true);
				uiex->setEnabled("pushButtonCancel", true);
				m_bCalibratingDome = false;
				m_bHomingDome = false;
				uiex->setText("pushButton", "Calibrate");
                // read step per rev from controller
                uiex->setPropertyInt("ticksPerRev","value", m_RTIDome.getNbTicksPerRev());
			}

            if(m_bSettingPanID) {
                nErr = m_RTIDome.isPanIdSet(m_nPanId, bComplete);
                if(bComplete) {
                    uiex->setEnabled("pushButton_2", true);
                    m_bSettingPanID = false;
                    m_RTIDome.getShutterSpeed(nSpeed);
                    uiex->setPropertyInt("shutterSpeed","value", nSpeed);
                    m_RTIDome.getShutterAcceleration(nAcc);
                    uiex->setPropertyInt("shutterAcceleration","value", nAcc);
                } else {
                    if(m_SetPanIdTimer.GetElapsedSeconds()>PANID_TIMEOUT ) {// 15 seconds is way more than needed.. something when wrong
                        snprintf(szErrorMessage, LOG_BUFFER_SIZE, "Timeout setting Xbee PAN ID");
                        uiex->messageBox("RTI-Dome Set PanID", szErrorMessage);
                        uiex->setEnabled("pushButton_2", true);
                        m_bSettingPanID = false;
                        m_RTIDome.getPanId(m_nPanId);
                        uiex->setPropertyInt("panID", "value", m_nPanId);
                        return;
                    }
                }
            }
            else if(m_bHasShutterControl && !m_bHomingDome && !m_bCalibratingDome) {
                // don't ask to often
                if (!(m_nBattRequest%4)) {
                    m_RTIDome.getBatteryLevels(dDomeBattery, dDomeCutOff, dShutterBattery, dShutterCutOff);
                    snprintf(szTmpBuf,16,"%2.2f V",dDomeBattery);
                    uiex->setPropertyString("domeBatteryLevel","text", szTmpBuf);
                    if(m_bHasShutterControl) {
                        if(dShutterBattery>=0.0f)
                            snprintf(szTmpBuf,16,"%2.2f V",dShutterBattery);
                        else
                            snprintf(szTmpBuf,16,"--");
                        uiex->setPropertyString("shutterBatteryLevel","text", szTmpBuf);
                    }
                    else {
                        snprintf(szTmpBuf,16,"NA");
                        uiex->setPropertyString("shutterBatteryLevel","text", szTmpBuf);
                    }
                }
                m_nBattRequest++;
                nErr = m_RTIDome.getRainSensorStatus(nRainSensorStatus);
                if(nErr)
                    uiex->setPropertyString("rainStatus","text", "--");
                else {
                    snprintf(szTmpBuf, 16, nRainSensorStatus==NOT_RAINING ? "Not raining" : "Raining");
                    uiex->setPropertyString("rainStatus","text", szTmpBuf);
                }
            }
        }
    }

    if (!strcmp(pszEvent, "on_pushButton_clicked"))
    {
        if(m_bLinked) {
            if(m_bHomingDome || m_bCalibratingDome) { // Abort
                // enable buttons
                uiex->setEnabled("pushButtonOK", true);
                uiex->setEnabled("pushButtonCancel", true);
                uiex->setEnabled("pushButton_2", true);
                // stop everything
                m_RTIDome.abortCurrentCommand();
                m_bHomingDome = false;
                m_bCalibratingDome = false;
                // set button text the Calibrate
                uiex->setText("pushButton", "Calibrate");
				// restore saved ticks per rev
				uiex->setPropertyInt("ticksPerRev","value", m_nSavedTicksPerRev);
				m_RTIDome.setNbTicksPerRev(m_nSavedTicksPerRev);
            } else {								// Calibrate
                // disable buttons
                uiex->setEnabled("pushButtonOK", false);
                uiex->setEnabled("pushButtonCancel", false);
                uiex->setEnabled("pushButton_2", false);
                // change "Calibrate" to "Abort"
                uiex->setText("pushButton", "Abort");
				m_nSavedTicksPerRev = m_RTIDome.getNbTicksPerRev();
				m_RTIDome.setNbTicksPerRev(16000000L);    // set this to a large value as the firmware only do 1 move of 1.5 time the current step per rev
                m_RTIDome.goHome();
                m_bHomingDome = true;
            }
        }
    }
    
    if (!strcmp(pszEvent, "on_pushButton_2_clicked")) {
        // set Pan ID
        uiex->propertyInt("panID", "value", nPanId);
        nErr = m_RTIDome.setPanId(nPanId);
        if(nErr) {
            snprintf(szErrorMessage, LOG_BUFFER_SIZE, "Error setting Xbee PAN ID : Error %d", nErr);
            uiex->messageBox("RTI-Dome Set PanID", szErrorMessage);
            return;
        }
        m_nPanId = nPanId;
        uiex->setEnabled("pushButton_2", false);
        m_bSettingPanID = true;
        m_SetPanIdTimer.Reset();
    }

    if (!strcmp(pszEvent, "on_pushButton_3_clicked")) {
        m_RTIDome.restoreDomeMotorSettings();
        // read values from dome controller
        n_nbStepPerRev = m_RTIDome.getNbTicksPerRev();
        uiex->setPropertyInt("ticksPerRev","value", n_nbStepPerRev);

        m_RTIDome.getRotationSpeed(nSpeed);
        uiex->setPropertyInt("rotationSpeed","value", nSpeed);

        m_RTIDome.getRotationAcceleration(nAcc);
        uiex->setPropertyInt("rotationAcceletation","value", nAcc);
    }

    if (!strcmp(pszEvent, "on_pushButton_4_clicked")) {
        m_RTIDome.restoreShutterMotorSettings();
        // read values from dome controller
        m_RTIDome.getShutterSpeed(nSpeed);
        uiex->setPropertyInt("shutterSpeed","value", nSpeed);

        m_RTIDome.getShutterAcceleration(nAcc);
        uiex->setPropertyInt("shutterAcceleration","value", nAcc);
    }

    if (!strcmp(pszEvent, "on_checkBox_stateChanged"))
    {
        m_bLogRainStatus = uiex->isChecked("checkBox");
        if(m_bLogRainStatus) {
            m_RTIDome.getRainStatusFileName(fName);
            uiex->setPropertyString("filePath","text", fName.c_str());
        }
        else {
            uiex->setPropertyString("filePath","text", "");
        }
    }

}

//
//HardwareInfoInterface
//
#pragma mark - HardwareInfoInterface

void X2Dome::deviceInfoNameShort(BasicStringInterface& str) const
{
	str = "RTI-Dome";
}

void X2Dome::deviceInfoNameLong(BasicStringInterface& str) const
{
    str = "RTI-Dome";
}

void X2Dome::deviceInfoDetailedDescription(BasicStringInterface& str) const
{
    str = "RTI-Dome Dome Controller";
}

 void X2Dome::deviceInfoFirmwareVersion(BasicStringInterface& str)
{

    if(m_bLinked) {
        char cFirmware[SERIAL_BUFFER_SIZE];
		X2MutexLocker ml(GetMutex());
        m_RTIDome.getFirmwareVersion(cFirmware, SERIAL_BUFFER_SIZE);
        str = cFirmware;

    }
    else
        str = "N/A";
}

void X2Dome::deviceInfoModel(BasicStringInterface& str)
{
    str = "RTI-Dome Dome Controller";
}

//
//DriverInfoInterface
//
#pragma mark - DriverInfoInterface

 void	X2Dome::driverInfoDetailedInfo(BasicStringInterface& str) const
{
    str = "RTI-Dome Dome Controller X2 plugin by Rodolphe Pineau";
}

double	X2Dome::driverInfoVersion(void) const
{
	return DRIVER_VERSION;
}

//
//DomeDriverInterface
//
#pragma mark - DomeDriverInterface

int X2Dome::dapiGetAzEl(double* pdAz, double* pdEl)
{
    if(!m_bLinked)
        return ERR_NOLINK;

	X2MutexLocker ml(GetMutex());

    *pdAz = m_RTIDome.getCurrentAz();
    *pdEl = m_RTIDome.getCurrentEl();
    return SB_OK;
}

int X2Dome::dapiGotoAzEl(double dAz, double dEl)
{
    int nErr;

    if(!m_bLinked)
        return ERR_NOLINK;

	X2MutexLocker ml(GetMutex());

    nErr = m_RTIDome.gotoAzimuth(dAz);
    if(nErr)
        return ERR_CMDFAILED;

    else
        return SB_OK;
}

int X2Dome::dapiAbort(void)
{
    if(!m_bLinked)
        return ERR_NOLINK;

	X2MutexLocker ml(GetMutex());

    m_RTIDome.abortCurrentCommand();

    return SB_OK;
}

int X2Dome::dapiOpen(void)
{
    int nErr;

    if(!m_bLinked)
        return ERR_NOLINK;

	if(!m_bHasShutterControl)
        return SB_OK;

	X2MutexLocker ml(GetMutex());

    nErr = m_RTIDome.openShutter();
    if(nErr)
        return ERR_CMDFAILED;

	return SB_OK;
}

int X2Dome::dapiClose(void)
{
    int nErr;

    if(!m_bLinked)
        return ERR_NOLINK;

    if(!m_bHasShutterControl)
        return SB_OK;

	X2MutexLocker ml(GetMutex());

    nErr = m_RTIDome.closeShutter();
    if(nErr)
        return ERR_CMDFAILED;

	return SB_OK;
}

int X2Dome::dapiPark(void)
{
    int nErr;

    if(!m_bLinked)
        return ERR_NOLINK;

	X2MutexLocker ml(GetMutex());

    nErr = m_RTIDome.parkDome();
    if(nErr)
        return ERR_CMDFAILED;

	return SB_OK;
}

int X2Dome::dapiUnpark(void)
{
    int nErr;

    if(!m_bLinked)
        return ERR_NOLINK;

	X2MutexLocker ml(GetMutex());

    nErr = m_RTIDome.unparkDome();
    if(nErr)
        return ERR_CMDFAILED;

	return SB_OK;
}

int X2Dome::dapiFindHome(void)
{
    int nErr;

    if(!m_bLinked)
        return ERR_NOLINK;

	X2MutexLocker ml(GetMutex());

	nErr = m_RTIDome.goHome();
    if(nErr)
        return ERR_CMDFAILED;

    return SB_OK;
}

int X2Dome::dapiIsGotoComplete(bool* pbComplete)
{
    int nErr;

    if(!m_bLinked)
        return ERR_NOLINK;

	X2MutexLocker ml(GetMutex());

	nErr = m_RTIDome.isGoToComplete(*pbComplete);
    if(nErr)
        return ERR_CMDFAILED;
    return SB_OK;
}

int X2Dome::dapiIsOpenComplete(bool* pbComplete)
{
    int nErr;

    if(!m_bLinked)
        return ERR_NOLINK;

    if(!m_bHasShutterControl)
    {
        *pbComplete = true;
        return SB_OK;
    }

	X2MutexLocker ml(GetMutex());

	nErr = m_RTIDome.isOpenComplete(*pbComplete);
    if(nErr)
        return ERR_CMDFAILED;

    return SB_OK;
}

int	X2Dome::dapiIsCloseComplete(bool* pbComplete)
{
    int nErr;

    if(!m_bLinked)
        return ERR_NOLINK;

    if(!m_bHasShutterControl)
    {
        *pbComplete = true;
        return SB_OK;
    }

	X2MutexLocker ml(GetMutex());

	nErr = m_RTIDome.isCloseComplete(*pbComplete);
    if(nErr)
        return ERR_CMDFAILED;

    return SB_OK;
}

int X2Dome::dapiIsParkComplete(bool* pbComplete)
{
    int nErr;

    if(!m_bLinked)
        return ERR_NOLINK;

	X2MutexLocker ml(GetMutex());

	nErr = m_RTIDome.isParkComplete(*pbComplete);
    if(nErr)
        return ERR_CMDFAILED;

    return SB_OK;
}

int X2Dome::dapiIsUnparkComplete(bool* pbComplete)
{
    int nErr;

    if(!m_bLinked)
        return ERR_NOLINK;

	X2MutexLocker ml(GetMutex());

	nErr = m_RTIDome.isUnparkComplete(*pbComplete);
    if(nErr)
        return ERR_CMDFAILED;

    return SB_OK;
}

int X2Dome::dapiIsFindHomeComplete(bool* pbComplete)
{
    int nErr;

    if(!m_bLinked)
        return ERR_NOLINK;

	X2MutexLocker ml(GetMutex());

	nErr = m_RTIDome.isFindHomeComplete(*pbComplete);
    if(nErr)
        return ERR_CMDFAILED;

    return SB_OK;
}

int X2Dome::dapiSync(double dAz, double dEl)
{
    int nErr;

    if(!m_bLinked)
        return ERR_NOLINK;

	X2MutexLocker ml(GetMutex());

	nErr = m_RTIDome.syncDome(dAz, dEl);
    if(nErr)
        return ERR_CMDFAILED;
	return SB_OK;
}

//
// SerialPortParams2Interface
//
#pragma mark - SerialPortParams2Interface

void X2Dome::portName(BasicStringInterface& str) const
{
    char szPortName[SERIAL_BUFFER_SIZE];

    portNameOnToCharPtr(szPortName, SERIAL_BUFFER_SIZE);

    str = szPortName;

}

void X2Dome::setPortName(const char* szPort)
{
    if (m_pIniUtil)
        m_pIniUtil->writeString(PARENT_KEY, CHILD_KEY_PORTNAME, szPort);

}


void X2Dome::portNameOnToCharPtr(char* pszPort, const int& nMaxSize) const
{
    if (NULL == pszPort)
        return;

    snprintf(pszPort, nMaxSize,DEF_PORT_NAME);

    if (m_pIniUtil)
        m_pIniUtil->readString(PARENT_KEY, CHILD_KEY_PORTNAME, pszPort, pszPort, nMaxSize);

}



