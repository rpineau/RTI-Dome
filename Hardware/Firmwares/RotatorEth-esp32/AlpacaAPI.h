// Alpaca API functions
//
//  Created by Rodolphe Pineau on 2024/04/16
//  Copyright © 2024 Rodolphe Pineau. All rights reserved.
//

#pragma message "Alpaca server enabled"
#include <atomic>
#include <vector>
#include <functional>
#include <EthernetUdp.h>
#include <ArduinoJson.h>
// Alpaca REST server
#include <UUID.h>
#include <aWOT.h>

// test
#include <LittleFS.h>

#define ALPACA_DISCOVERY_PORT 32227
#define ALPACA_SERVER_PORT 80
#define ALPACA_VAR_BUF_LEN 256
#define ALPACA_OK 0
#define DISCOVERY_ERROR -1
#define DOME_INTERFACE_VERSION 3

enum ShutterStates { OPEN, CLOSED, OPENING, CLOSING, BOTTOM_OPEN, BOTTOM_CLOSED, BOTTOM_OPENING, BOTTOM_CLOSING, ERROR, FINISHING_OPEN, FINISHING_CLOSE };
enum AlpacaShutterStates { A_OPEN=0, A_CLOSED, A_OPENING, A_CLOSING,  A_ERROR};

uint32_t nTransactionID;
UUID uuid;
String sAlpacaDiscovery = "alpacadiscovery1";
String sRedirectURL;
volatile bool bAlpacaConnected = false;


class DomeAlpacaDiscoveryServer
{
public:
	DomeAlpacaDiscoveryServer(int port=ALPACA_DISCOVERY_PORT);
	void startServer();
	int checkForRequest();
private:
	EthernetUDP *discoveryServer;
	int m_UDPPort;
};

// ALPACA discovery server
DomeAlpacaDiscoveryServer::DomeAlpacaDiscoveryServer(int port)
{
	m_UDPPort = port;
	discoveryServer = nullptr;
}

void DomeAlpacaDiscoveryServer::startServer()
{
	discoveryServer = new EthernetUDP();
	if(!discoveryServer) {
		discoveryServer = nullptr;
		return;
	}
	discoveryServer->begin(m_UDPPort);
	DBPrintln("Alpaca discovery server started on port " + String(m_UDPPort));
}

int DomeAlpacaDiscoveryServer::checkForRequest()
{
	if(!discoveryServer)
		return -1;

	String sDiscoveryResponse = "{\"AlpacaPort\":"+String(ALPACA_SERVER_PORT)+"}";
	String sDiscoveryRequest;

	char packetBuffer[UDP_TX_PACKET_MAX_SIZE];
	int packetSize = discoveryServer->parsePacket();
	if (packetSize) {
		DBPrintln("Alpaca discovery server request");
		memset(packetBuffer,0,sizeof(packetBuffer));
		discoveryServer->read(packetBuffer, UDP_TX_PACKET_MAX_SIZE);
		// do stuff
		sDiscoveryRequest = String(packetBuffer);
		DBPrintln("Alpaca discovery server sDiscoveryRequest : " + sDiscoveryRequest);
		if(sDiscoveryRequest.indexOf(sAlpacaDiscovery)==-1) {
			DBPrintln("Alpaca discovery server request error");
			return DISCOVERY_ERROR; // wrong type of discovery message
		}
		DBPrintln("Alpaca discovery server sending response : " + sDiscoveryResponse);
		// send discovery reponse
		discoveryServer->beginPacket(discoveryServer->remoteIP(), discoveryServer->remotePort());
		discoveryServer->write(sDiscoveryResponse.c_str());
		discoveryServer->endPacket();
	}
	return ALPACA_OK;
}


int getAlpacaShutterState()
{
	int nAlpacaShutterState = A_ERROR;
	String sTmpString;
	if(!nbWiFiClient) {
				nAlpacaShutterState = A_ERROR;
	} else
	{
		shutterClient.print(sTmpString + "#");
		ReceiveWiFi(shutterClient);
		switch (RemoteShutter.state) {
			case OPEN:
				nAlpacaShutterState = A_OPEN;
				break;
			case CLOSED:
				nAlpacaShutterState = A_CLOSED;
				break;
			case ERROR:
				nAlpacaShutterState = A_ERROR;
				break;
			case OPENING:
			case BOTTOM_OPEN:
			case BOTTOM_OPENING:
			case FINISHING_OPEN:
				nAlpacaShutterState = A_OPENING;
				break;
			case CLOSING:
			case BOTTOM_CLOSED:
			case BOTTOM_CLOSING:
			case FINISHING_CLOSE:
				nAlpacaShutterState = A_CLOSING;
				break;
			default:
				nAlpacaShutterState = A_ERROR;
				break;
		}

	}
	return nAlpacaShutterState;

}

void formDataToJson(Request &req, JsonDocument &FormData)
{
	char name[ALPACA_VAR_BUF_LEN];
	char value[ALPACA_VAR_BUF_LEN];
	String sName;
	String sValue;
	memset(name,0,ALPACA_VAR_BUF_LEN);
	memset(value,0,ALPACA_VAR_BUF_LEN);

	while(req.form(name, ALPACA_VAR_BUF_LEN-1, value, ALPACA_VAR_BUF_LEN-1)){
		sName  = String(name);
		sName.toLowerCase();
		sValue = String(value);
		sValue.toLowerCase();
		DBPrintln("name : " + sName);
		DBPrintln("value : " + sValue);

		if(isDigit(value[0]) ) {
			if(sValue.indexOf('.') == -1) {
				// int
				FormData[sName]=sValue.toInt();
			} else {
				// float
				FormData[sName]=sValue.toFloat();
			}
		}
		else {
			// string
			// check for boolean
			if(sValue == "true") {
				FormData[sName]=true;
			}
			else if(sValue == "false") {
				FormData[sName]=false;
			}
			else {
				FormData[sName]=sValue;
			}
		}
	}
}


void  getQueryGetVariables(String sQueryString, std::vector<std::vector<String>> &svParameters)
{
	int nErr;
	int nIndex = 0;
	int nCurIndex = 0;
	String sEntry;
	std::vector<String> svKV;
	std::vector<String> svFields;

	DBPrintln("getQueryGetVariables");

	// url parameters are separate by '&'
	while(true) {
		nIndex = sQueryString.indexOf('&',nCurIndex);
		if(nIndex == -1) {
			svFields.push_back(sQueryString.substring(nCurIndex));
			break;
		}
		svFields.push_back(sQueryString.substring(nCurIndex,nIndex));
		nCurIndex = nIndex+1;
	}
	if(svFields.size()) {
		// now split each field in key,value pair with '=' as the separator
		for(String &sTmp : svFields) {
			sTmp.toLowerCase();
			nIndex = sTmp.indexOf('=');
			svKV.push_back(sTmp.substring(0,nIndex));
			svKV.push_back(sTmp.substring(nIndex+1));
			svParameters.push_back(svKV);
			svKV.clear();
		}
	}
	return;
}

bool getIDs(Request &req, JsonDocument &AlpacaResp, JsonDocument &FormData)
{
	char ClientID[64];
	char ClientTransactionID[64];
	String sClientId;
	String sClientTransactionId;
	std::vector<std::vector<String>> svParameters;

	bool bParamOk = true;

	DBPrintln("getIDs");

	AlpacaResp["ServerTransactionID"] = nTransactionID;

	if(req.method() == Request::GET) {
		// the req.query being case sensitive will not work here.
		getQueryGetVariables(String(req.query()), svParameters);
		for( std::vector<String> &svParamEntry : svParameters ) {

			if(svParamEntry.at(0).equals("clientid"))
				sClientId = svParamEntry.at(1);
			if(svParamEntry.at(0).equals("clienttransactionid"))
				sClientTransactionId = svParamEntry.at(1);
		}

		if(sClientId.length())
			AlpacaResp["ClientID"] = sClientId.toInt()<0?0:sClientId.toInt();
		if(sClientTransactionId.length())
			AlpacaResp["ClientTransactionID"] = sClientTransactionId.toInt()<0?0:sClientTransactionId.toInt();
	}
	else { // this is a PUT, therefore there should be some form data
		formDataToJson(req, FormData);
		if(FormData.size()==0){
			bParamOk = false;
		}
		else {
			if(FormData["clientid"].is<unsigned long>()) {
				serializeJson(FormData["clientid"], sClientId);
				sClientId.trim();
				AlpacaResp["ClientID"] = sClientId.toInt()<0?0:sClientId.toInt();

			}
			if(FormData["clienttransactionid"].is<unsigned long>()) {
				serializeJson(FormData["clienttransactionid"], sClientTransactionId);
				sClientTransactionId.trim();
				AlpacaResp["ClientTransactionID"] = sClientTransactionId.toInt()<0?0:sClientTransactionId.toInt();
			}
		}
#ifdef DEBUG
		String sTmp;
		serializeJson(FormData, sTmp);
		DBPrintln("FormData : " + sTmp);
		DBPrintln("FormData.size() : " + String(FormData.size()));
#endif
	}

	DBPrintln("bParamOk : " + String(bParamOk?"Ok":"Error"));
	DBPrintln("sClientId : " + sClientId);
	DBPrintln("sClientTransactionId : " + sClientTransactionId);

	return bParamOk;
}

void redirectToSetup(Request &req, Response &res)
{
	res.set("Location", sRedirectURL.c_str());
    res.sendStatus(302);
}

void getApiVersion(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** getApiVersion ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);

	res.set("Content-Type", "application/json");

	AlpacaResp["Value"][0] = 1;

	serializeJson(AlpacaResp, sResp);

	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void getDescription(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** getDescription ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);

	res.set("Content-Type", "application/json");
	AlpacaResp["Value"]["ServerName"]= "RTIDome Alpaca";
	AlpacaResp["Value"]["Manufacturer"]= "RTI-Zone";
	AlpacaResp["Value"]["ManufacturerVersion"]= VERSION;
	AlpacaResp["Value"]["Location"]= "Earth";

	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void getConfiguredDevice(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** getConfiguredDevice ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);

	res.set("Content-Type", "application/json");
	AlpacaResp["Value"][0] ["DeviceName"]= "RTIDome";
	AlpacaResp["Value"][0] ["DeviceType"]= "dome";
	AlpacaResp["Value"][0] ["DeviceNumber"]= 0;
	AlpacaResp["Value"][0] ["UniqueID"]= uuid;

	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void doAction(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;
	String sAction;
	String sParameters;

	DBPrintln("[ ********** doAction ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);

	res.set("Content-Type", "application/json");

	if(!bParamsOk){
		AlpacaResp["ErrorNumber"] = 0x401;
		AlpacaResp["ErrorMessage"] = "Invalid parameters";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
			return;
	}

	serializeJson(FormData["action"], sAction);
	serializeJson(FormData["parameters"], sParameters);
#ifdef DEBUG
	DBPrintln("sAction : " + sAction);
	DBPrintln("sParameters : " + sParameters);
#endif

	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	AlpacaResp["Value"] = "Ok";
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void doCommandBlind(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;
	String sClientId;
	String sClientTransactionId;

	DBPrintln("[ ********** doCommandBlind ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);

	res.set("Content-Type", "application/json");

	if(!bParamsOk){
		AlpacaResp["ErrorNumber"] = 0x401;
		AlpacaResp["ErrorMessage"] = "Invalid parameters";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
			return;
	}

	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void doCommandBool(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;
	String sClientId;
	String sClientTransactionId;

	DBPrintln("[ ********** doCommandBool ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);

	res.set("Content-Type", "application/json");

	if(!bParamsOk){
		AlpacaResp["ErrorNumber"] = 0x401;
		AlpacaResp["ErrorMessage"] = "Invalid parameters";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
			return;
	}

	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	AlpacaResp["Value"] = true;
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void doCommandString(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;
	String sClientId;
	String sClientTransactionId;

	DBPrintln("[ ********** doCommandString ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);

	res.set("Content-Type", "application/json");

	if(!bParamsOk){
		AlpacaResp["ErrorNumber"] = 0x401;
		AlpacaResp["ErrorMessage"] = "Invalid parameters";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
			return;
	}

	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	AlpacaResp["Value"] = "Ok";
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void getConnected(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** getConected ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);

	res.set("Content-Type", "application/json");
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	AlpacaResp["Value"] = bAlpacaConnected;
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void setConnected(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;
	String sClientId;
	String sClientTransactionId;
	String sParameter;
	String sTmp;

	DBPrintln("[ ********** setConected ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);
	res.set("Content-Type", "application/json");

	if(!bParamsOk){
		AlpacaResp["ErrorNumber"] = 0x401;
		AlpacaResp["ErrorMessage"] = "Invalid parameters";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
			return;
	}

	if(!FormData["connected"].is<bool>()) {
		AlpacaResp["ErrorNumber"] = 0x401;
		AlpacaResp["ErrorMessage"] = "Invalid parameters, missing 'Connected'";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
			return;
	}

	bAlpacaConnected = FormData["connected"];
	DBPrintln("bAlpacaConnected : " + (bAlpacaConnected?String("true"):String("false")));

	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void domeConnect(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;
	String sClientId;
	String sClientTransactionId;
	String sParameter;
	String sTmp;

	DBPrintln("[ ********** setConected ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);
	res.set("Content-Type", "application/json");

	if(!bParamsOk){
		AlpacaResp["ErrorNumber"] = 0x401;
		AlpacaResp["ErrorMessage"] = "Invalid parameters";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
			return;
	}

	bAlpacaConnected = true;
	DBPrintln("bAlpacaConnected : " + (bAlpacaConnected?String("true"):String("false")));

	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void domeConnecting(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;
	String sClientId;
	String sClientTransactionId;
	String sParameter;
	String sTmp;

	DBPrintln("[ ********** setConected ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);
	res.set("Content-Type", "application/json");

	if(!bParamsOk){
		AlpacaResp["ErrorNumber"] = 0x401;
		AlpacaResp["ErrorMessage"] = "Invalid parameters";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
			return;
	}

	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	AlpacaResp["Value"] = false; // it's already connected
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}



void getDomeState(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument jsTmp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;
	float Alt, Az;
	float dParkPos, dCurrentAz;
	bool bParked = false;

	DBPrintln("[ ********** getDomeState ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);

	res.set("Content-Type", "application/json");
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	// add states to response
	switch (RemoteShutter.state ) {
		case OPEN:
			Alt = 90.0f;
			break;
		case CLOSED:
			Alt = 0.0f;
			break;
		default:
			Alt = 0.0f;
			break;
	}
	jsTmp["Altitude"] = Alt;
	AlpacaResp["Value"].add(jsTmp);

	jsTmp.clear();
	jsTmp["AtHome"] = (Rotator->GetHomeStatus() == ATHOME);
	AlpacaResp["Value"].add(jsTmp);

	jsTmp.clear();
	dParkPos = Rotator->GetParkAzimuth();
	dCurrentAz = Rotator->GetAzimuth();
	if(Rotator->checkBoundaries(dParkPos, dCurrentAz, 1.0)) {
		bParked = true;
	}
	jsTmp["AtPark"] = bParked;
	AlpacaResp["Value"].add(jsTmp);

	jsTmp.clear();
	jsTmp["Azimuth"] = Rotator->GetAzimuth();
	AlpacaResp["Value"].add(jsTmp);

	jsTmp.clear();
	jsTmp["ShutterStatus"] = getAlpacaShutterState();
	AlpacaResp["Value"].add(jsTmp);

	jsTmp.clear();
	if(Rotator->GetSeekMode() != NOT_MOVING) {
		AlpacaResp["Slewing"] = true;
	}
	else {
		AlpacaResp["Slewing"] = false;
	}
	AlpacaResp["Value"].add(jsTmp);

	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void domeDisconnect(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;
	String sClientId;
	String sClientTransactionId;
	String sParameter;
	String sTmp;

	DBPrintln("[ ********** setConected ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);
	res.set("Content-Type", "application/json");

	if(!bParamsOk){
		AlpacaResp["ErrorNumber"] = 0x401;
		AlpacaResp["ErrorMessage"] = "Invalid parameters";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
			return;
	}

	bAlpacaConnected = false;
	DBPrintln("bAlpacaConnected : " + (bAlpacaConnected?String("true"):String("false")));

	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void getDeviceDescription(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** getDeviceDescription ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);

	res.set("Content-Type", "application/json");
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	AlpacaResp["Value"]= "RTI-Zone dome controller";
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void getDriverInfo(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** getDriverInfo ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);

	res.set("Content-Type", "application/json");
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	AlpacaResp["Value"]= "RTI-Zone Dome controller";
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void getDriverVersion(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** getDriverVersion ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);

	res.set("Content-Type", "application/json");

	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	AlpacaResp["Value"]= String(VERSION);
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void getInterfaceVersion(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** getInterfaceVersion ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);

	res.set("Content-Type", "application/json");
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	AlpacaResp["Value"]= DOME_INTERFACE_VERSION;
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void getName(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** getName ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);

	res.set("Content-Type", "application/json");
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	AlpacaResp["Value"]= "RTI-Zone Dome controller";
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void getSupportedActions(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** getSupportedActions ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);

	res.set("Content-Type", "application/json");

	AlpacaResp["Value"] = "[]";
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void getAltitude(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	String sTmpString = String(STATE_SHUTTER);

	DBPrintln("[ ********** getAltitude ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);

	res.set("Content-Type", "application/json");
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";

#ifdef USE_WIFI
	shutterClient.print(sTmpString + "#");
	ReceiveWiFi(shutterClient);
	switch (RemoteShutter.state ) {
		case OPEN:
			AlpacaResp["Value"] = 90.0f;
			break;
		case CLOSED:
			AlpacaResp["Value"] = 0.0f;
			break;
		default:
			AlpacaResp["Value"] = 0.0f;
			break;
	}
#else
	AlpacaResp["Value"] = 0.0f;
#endif
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());

}

void geAtHome(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** geAtHome ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);

	res.set("Content-Type", "application/json");
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";

	if(String(Rotator->GetHomeStatus() == ATHOME)) {
		AlpacaResp["Value"] = true;
	}
	else {
		AlpacaResp["Value"] = false;
	}
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());

}

void geAtPark(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** geAtPark ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);

	res.set("Content-Type", "application/json");

	if(bParked) {
		AlpacaResp["Value"] = true;
	}
	else {
		AlpacaResp["Value"] = false;
	}
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());

}

void getAzimuth(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** getAzimuth ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);

	res.set("Content-Type", "application/json");
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";

	AlpacaResp["Value"] = Rotator->GetAzimuth();

	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void canfindhome(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** canfindhome ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);

	res.set("Content-Type", "application/json");
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";

	AlpacaResp["Value"] = true;

	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void canPark(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** canPark ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);

	res.set("Content-Type", "application/json");
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";

	AlpacaResp["Value"] = true;

	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void canSetAltitude(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** canSetAltitude ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);

	res.set("Content-Type", "application/json");
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";

	AlpacaResp["Value"] = false;

	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void canSetAzimuth(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** canSetAzimuth ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);

	res.set("Content-Type", "application/json");
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";

	AlpacaResp["Value"] = true;

	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void canSetPark(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** canSetPark ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);

	res.set("Content-Type", "application/json");
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";

	AlpacaResp["Value"] = true;

	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void canSetShutter(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** canSetShutter ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);

	res.set("Content-Type", "application/json");
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";

#ifndef USE_WIFI
	AlpacaResp["Value"] = false;
#else
	if(!nbWiFiClient) {
		AlpacaResp["Value"] = true;
	}
	else {
		AlpacaResp["Value"] = true;
	}
#endif
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void canSlave(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** canSlave ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);

	res.set("Content-Type", "application/json");
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";

	AlpacaResp["Value"] = false;

	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void canSyncAzimuth(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** canSyncAzimuth ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);

	res.set("Content-Type", "application/json");
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";

	AlpacaResp["Value"] = true;

	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void getShutterStatus(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	String sTmpString = String(STATE_SHUTTER);

	DBPrintln("[ ********** getShutterStatus ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);

	res.set("Content-Type", "application/json");

#ifndef USE_WIFI
	AlpacaResp["ErrorNumber"] = 0x400;
	AlpacaResp["ErrorMessage"] = "Not implemented";
#else
	if(!nbWiFiClient) {
		AlpacaResp["ErrorNumber"] = 0x40B;
		AlpacaResp["ErrorMessage"] = "Shutter not connected";
	} else {
		AlpacaResp["ErrorNumber"] = 0;
		AlpacaResp["ErrorMessage"] = "";
		shutterClient.print(sTmpString + "#");
		ReceiveWiFi(shutterClient);
		AlpacaResp["Value"] = getAlpacaShutterState();
	}
#endif
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}
void getSlaved(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** canSlave ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);

	res.set("Content-Type", "application/json");
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";

	AlpacaResp["Value"] = false;

	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void setSlaved(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** Slaved ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);
	res.set("Content-Type", "application/json");

	AlpacaResp["ErrorNumber"] = 0x400;
	AlpacaResp["ErrorMessage"] = "Invalid parameters, missing 'Connected'";
	AlpacaResp["Value"] = false;

	serializeJson(AlpacaResp, sResp);
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void getSlewing(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** getSlewing ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);

	res.set("Content-Type", "application/json");
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";

	DBPrintln("Seekmode : " + String(Rotator->GetSeekMode()));

	if(Rotator->GetSeekMode() != NOT_MOVING) {
		AlpacaResp["Value"] = true;
	}
	else {
		AlpacaResp["Value"] = false;
	}

	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void doAbort(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** doAbort ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);

	res.set("Content-Type", "application/json");

	if(!bParamsOk){
		AlpacaResp["ErrorNumber"] = 0x401;
		AlpacaResp["ErrorMessage"] = "Invalid parameters";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
			return;
	}

	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";

	Abort(); // this is in the RotatorEth-esp32.ino

	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void doCloseShutter(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	String sTmpString = String(CLOSE_SHUTTER);

	DBPrintln("[ ********** doCloseShutter ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);

	res.set("Content-Type", "application/json");

	if(!bParamsOk){
		AlpacaResp["ErrorNumber"] = 0x401;
		AlpacaResp["ErrorMessage"] = "Invalid parameters";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
			return;
	}

#ifndef USE_WIFI
	AlpacaResp["ErrorNumber"] = 0x400;
	AlpacaResp["ErrorMessage"] = "Not implemented";
#else
	if(!nbWiFiClient) {
		AlpacaResp["ErrorNumber"] = 0x40B;
		AlpacaResp["ErrorMessage"] = "Shutter not connected";
	} else {
		AlpacaResp["ErrorNumber"] = 0;
		AlpacaResp["ErrorMessage"] = "";
		shutterClient.print(sTmpString+ "#");
		ReceiveWiFi(shutterClient);
	}
#endif
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void doFindHome(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** doFindHome ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);

	res.set("Content-Type", "application/json");

	if(!bParamsOk){
		AlpacaResp["ErrorNumber"] = 0x401;
		AlpacaResp["ErrorMessage"] = "Invalid parameters";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
			return;
	}

	if(bLowShutterVoltage) {
		AlpacaResp["ErrorNumber"] = 0x408;
		AlpacaResp["ErrorMessage"] = "Low shutter voltage, staying at park position";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
			return;
	}

	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";

	Rotator->StartHoming();

	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void doOpenShutter(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	String sTmpString = String(OPEN_SHUTTER);

	DBPrintln("[ ********** doOpenShutter ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);

	res.set("Content-Type", "application/json");

	if(!bParamsOk){
		AlpacaResp["ErrorNumber"] = 0x401;
		AlpacaResp["ErrorMessage"] = "Invalid parameters";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
			return;
	}

#ifndef USE_WIFI
	AlpacaResp["ErrorNumber"] = 0x400;
	AlpacaResp["ErrorMessage"] = "Not implemented";
#else
	if(!nbWiFiClient) {
		AlpacaResp["ErrorNumber"] = 0x40B;
		AlpacaResp["ErrorMessage"] = "Shutter not connected";
	}
	else if(bLowShutterVoltage) {
		AlpacaResp["ErrorNumber"] = 0x408;
		AlpacaResp["ErrorMessage"] = "Low shutter voltage, staying at park position";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
		}
	else {
		AlpacaResp["ErrorNumber"] = 0;
		AlpacaResp["ErrorMessage"] = "";

		shutterClient.print(sTmpString+ "#");
		ReceiveWiFi(shutterClient);
	}
#endif
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void doPark(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;
	float fParkPos;

	DBPrintln("[ ********** doPark ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);

	res.set("Content-Type", "application/json");

	if(!bParamsOk){
		AlpacaResp["ErrorNumber"] = 0x401;
		AlpacaResp["ErrorMessage"] = "Invalid parameters";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
			return;
	}

	fParkPos = Rotator->GetParkAzimuth();
	Rotator->GoToAzimuth(fParkPos);
	bParked = true;

	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void setPark(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;
	float fParkPos;

	DBPrintln("[ ********** setPark ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);

	res.set("Content-Type", "application/json");

	if(!bParamsOk){
		AlpacaResp["ErrorNumber"] = 0x401;
		AlpacaResp["ErrorMessage"] = "Invalid parameters";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
			return;
	}


	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
	fParkPos = Rotator->GetAzimuth();
	Rotator->SetParkAzimuth(fParkPos);
}

void doAltitudeSlew(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** doAltitudeSlew ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);

	res.set("Content-Type", "application/json");

#ifndef USE_WIFI
	if(!bParamsOk){
		AlpacaResp["ErrorNumber"] = 0x401;
		AlpacaResp["ErrorMessage"] = "Invalid parameters";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
			return;
	}

	if(!FormData["altitude"].is<float>()) {
		AlpacaResp["ErrorNumber"] = 0x401;
		AlpacaResp["ErrorMessage"] = "Invalid value";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
			return;
	}
	// in case we implement this one day.
	AlpacaResp["ErrorNumber"] = 0x400;
	AlpacaResp["ErrorMessage"] = "Not implemented";
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);
#else
	AlpacaResp["ErrorNumber"] = 0x400;
	AlpacaResp["ErrorMessage"] = "Invalid method";
	serializeJson(AlpacaResp, sResp);
#endif

	res.write((uint8_t*)(sResp.c_str()),sResp.length());

}

void doGoTo(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;
	float dNewPos;

	DBPrintln("[ ********** doGoTo ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);

	res.set("Content-Type", "application/json");

	if(!bParamsOk){
		AlpacaResp["ErrorNumber"] = 0x401;
		AlpacaResp["ErrorMessage"] = "Invalid parameters";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
			return;
	}

	if(bLowShutterVoltage) {
		AlpacaResp["ErrorNumber"] = 0x408;
		AlpacaResp["ErrorMessage"] = "Low shutter voltage, staying at park position";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
			return;
	}

	if(!FormData["azimuth"].is<float>()) {
		AlpacaResp["ErrorNumber"] = 0x401;
		AlpacaResp["ErrorMessage"] = "Invalid parameters";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
			return;
	}

	dNewPos = FormData["azimuth"];
	if(dNewPos < 0.0f || dNewPos > 360.0f) {
		AlpacaResp["ErrorNumber"] = 1025;
		AlpacaResp["ErrorMessage"] = "Invalid azimuth";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
			return;
	}

	Rotator->GoToAzimuth(dNewPos);

	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void doSyncAzimuth(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;
	float dNewPos;

	DBPrintln("[ ********** doSyncAzimuth ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);

	res.set("Content-Type", "application/json");

	if(!bParamsOk){
		AlpacaResp["ErrorNumber"] = 0x401;
		AlpacaResp["ErrorMessage"] = "Invalid parameters";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
			return;
	}

	if(!FormData["azimuth"].is<float>()) {
		AlpacaResp["ErrorNumber"] = 1025;
		AlpacaResp["ErrorMessage"] = "Invalid azimuth";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
			return;
	}

	dNewPos = FormData["azimuth"];
	if(dNewPos < 0.0f || dNewPos > 360.0f) {
		AlpacaResp["ErrorNumber"] = 0x401;
		AlpacaResp["ErrorMessage"] = "Invalid Azimuth";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
			return;
	}

	Rotator->SyncPosition(dNewPos);

	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}


void doSetup(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;
	String sHTML;

	res.set("Content-Type", "text/html");

	DBPrintln("[ ********** doSetup ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);

	sHTML = "<!DOCTYPE html>\n<html>\n";
	sHTML += "<head>";
	sHTML += "<title>RTI Dome Setup</title>\n";
	sHTML += "</head>\n";
	sHTML += "<body>\n";

	sHTML += "<H1>RTI Dome Setup</H1>\n";

	// display passed data
	if(FormData.size()!=0){
		sHTML += "<p>data passed : </p>\n";
		sHTML += "<p>"+sResp+"</p>\n";
	}

	sHTML += "</body>\n</html>\n";
	res.print(sHTML);
}

//
// controller settings API
//
void homePosition(Request &req, Response &res)
{
	JsonDocument controllerResp;
	String sResp;
	float dPosition = 0.0f;

	if(req.method() == Request::PUT) {
		JsonDocument FormData;
		formDataToJson(req, FormData);
		if(FormData.size()==0){
		}
		else {
			if(FormData["value"].is<float>()) {
				dPosition = FormData["value"];
				Rotator->SetHomeAzimuth(dPosition);
			}
		}
	}

	controllerResp["value"] = Rotator->GetHomeAzimuth();
	serializeJson(controllerResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void parkPosition(Request &req, Response &res)
{
	JsonDocument controllerResp;
	String sResp;
	float dPosition = 0.0f;

	if(req.method() == Request::PUT) {
		JsonDocument FormData;
		formDataToJson(req, FormData);
		if(FormData.size()==0){
		}
		else {
			if(FormData["value"].is<float>()) {
				dPosition = FormData["value"];
				Rotator->SetParkAzimuth(dPosition);
			}
		}
	}

	controllerResp["value"] = Rotator->GetParkAzimuth();
	serializeJson(controllerResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void reverseDirectionState(Request &req, Response &res)
{
	JsonDocument controllerResp;
	String sResp;
	bool bReversed = false;

	if(req.method() == Request::PUT) {
		JsonDocument FormData;
		formDataToJson(req, FormData);
		if(FormData.size()==0){
		}
		else {
			if(FormData["value"].is<bool>()) {
				bReversed = FormData["value"];
				Rotator->SetReversed(bReversed);
			}
		}
	}

	controllerResp["value"] = Rotator->GetReversed();
	serializeJson(controllerResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}


void shutterOpenOrderValue(Request &req, Response &res)
{
	JsonDocument controllerResp;
	String sResp;
	bool bReversed = false;

	if(req.method() == Request::PUT) {
		JsonDocument FormData;
		formDataToJson(req, FormData);
		if(FormData.size()==0){
		}
		else {
			if(FormData["value"].is<int>()) {
				bReversed = FormData["value"];
				// need implementation
				// Rotator->;
			}
		}
	}
	// need implementation
	// controllerResp["value"] = Rotator->GetReversed();
	controllerResp["value"] = 1; // for now
	serializeJson(controllerResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}


void wifiSSIDValue(Request &req, Response &res)
{
	JsonDocument controllerResp;
	String sResp;
	WIFIConfig l_WifiConfig;
	bool bReversed = false;
	String SSID;

	Rotator->getWiFiConfig(l_WifiConfig);

	if(req.method() == Request::PUT) {
		JsonDocument FormData;
		formDataToJson(req, FormData);
		if(FormData.size()==0){
		}
		else {
			if(FormData["value"].is<String>()) {
				strncpy(l_WifiConfig.sSSID, FormData["value"], WIFI_VAR_LEN);
				// need implementation
				//Rotator->setWiFiConfig(l_WifiConfig);
				configureWiFi();

			}
		}
	}

	controllerResp["SSID"] = l_WifiConfig.sSSID;
	controllerResp["Ip"] = RotatorClass::IpAddress2String(l_WifiConfig.ip);
	controllerResp["Password"] = l_WifiConfig.sPassword;
	serializeJson(controllerResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}


void isShutterPresentState(Request &req, Response &res)
{
	JsonDocument controllerResp;
	String sResp;
	bool bReversed = false;


	controllerResp["value"] = bool(bShutterPresent);
	serializeJson(controllerResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}


void useDHCPState(Request &req, Response &res)
{
	JsonDocument controllerResp;
	String sResp;
	bool bUseDhcp = false;

	if(req.method() == Request::PUT) {
		JsonDocument FormData;
		formDataToJson(req, FormData);
		if(FormData.size()==0){
		}
		else {
			if(FormData["value"].is<bool>()) {
				bUseDhcp = FormData["value"];
				Rotator->setDHCPFlag(bUseDhcp);
			}
		}
	}

	controllerResp["value"] = Rotator->getDHCPFlag();
	serializeJson(controllerResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void macAddressValue(Request &req, Response &res)
{
	JsonDocument controllerResp;
	String sResp;

	controllerResp["value"] = String(MAC_Address[0], HEX) + String(":") +
					String(MAC_Address[1], HEX) + String(":") +
					String(MAC_Address[2], HEX) + String(":") +
					String(MAC_Address[3], HEX) + String(":") +
					String(MAC_Address[4], HEX) + String(":") +
					String(MAC_Address[5], HEX);

	serializeJson(controllerResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());

}

void ipAddressValue(Request &req, Response &res)
{
	JsonDocument controllerResp;
	String sResp;

	if(req.method() == Request::PUT) {
		JsonDocument FormData;
		formDataToJson(req, FormData);
		if(FormData.size()==0){
		}
		else {
			if(FormData["value"].is<String>()) {
				Rotator->setIPAddress(FormData["value"]);
			}
		}
	}

	controllerResp["value"] = String(RotatorClass::IpAddress2String(domeEthernet.localIP()));
	serializeJson(controllerResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void subnetMaskValue(Request &req, Response &res)
{
	JsonDocument controllerResp;
	String sResp;

	if(req.method() == Request::PUT) {
		JsonDocument FormData;
		formDataToJson(req, FormData);
		if(FormData.size()==0){
		}
		else {
			if(FormData["value"].is<String>()) {
				Rotator->setIPSubnet(FormData["value"]);
			}
		}
	}

	controllerResp["value"] = String(RotatorClass::IpAddress2String(domeEthernet.subnetMask()));
	serializeJson(controllerResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void ipGetewayValue(Request &req, Response &res)
{
	JsonDocument controllerResp;
	String sResp;

	if(req.method() == Request::PUT) {
		JsonDocument FormData;
		formDataToJson(req, FormData);
		if(FormData.size()==0){
		}
		else {
			if(FormData["value"].is<String>()) {
				Rotator->setIPGateway(FormData["value"]);
			}
		}
	}

	controllerResp["value"] = String(RotatorClass::IpAddress2String(domeEthernet.gatewayIP()));
	serializeJson(controllerResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void domeCalibrateAction(Request &req, Response &res)
{
	JsonDocument controllerResp;
	String sResp;

	if(req.method() == Request::PUT) {
		JsonDocument FormData;
		formDataToJson(req, FormData);
		if(FormData.size()==0){
		}
		else {
			if(FormData["value"].is<String>()) {
				if(FormData["value"] == "start") {
					Rotator->StartCalibrating();
				}
				if(FormData["value"] == "abort") {
					Rotator->Stop();
				}
			}
		}
	}

	controllerResp["value"] = String(RotatorClass::IpAddress2String(domeEthernet.gatewayIP()));
	serializeJson(controllerResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void stepPerRevolutionValue(Request &req, Response &res)
{
	JsonDocument controllerResp;
	String sResp;

	if(req.method() == Request::PUT) {
		JsonDocument FormData;
		formDataToJson(req, FormData);
		if(FormData.size()==0){
		}
		else {
			if(FormData["value"].is<long>()) {
				Rotator->SetStepsPerRotation(FormData["value"]);
			}
		}
	}

	controllerResp["value"] = Rotator->GetStepsPerRotation();
	serializeJson(controllerResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void rotationSpeedValue(Request &req, Response &res)
{
	JsonDocument controllerResp;
	String sResp;

	if(req.method() == Request::PUT) {
		JsonDocument FormData;
		formDataToJson(req, FormData);
		if(FormData.size()==0){
		}
		else {
			if(FormData["value"].is<long>()) {
				Rotator->SetMaxSpeed(FormData["value"]);
			}
		}
	}

	controllerResp["value"] = Rotator->GetMaxSpeed();
	serializeJson(controllerResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void rotationAccelerationValue(Request &req, Response &res)
{
	JsonDocument controllerResp;
	String sResp;

	if(req.method() == Request::PUT) {
		JsonDocument FormData;
		formDataToJson(req, FormData);
		if(FormData.size()==0){
		}
		else {
			if(FormData["value"].is<long>()) {
				Rotator->SetAcceleration(FormData["value"]);
			}
		}
	}

	controllerResp["value"] = Rotator->GetAcceleration();
	serializeJson(controllerResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}


void restoreRotationMotorValues(Request &req, Response &res)
{
	JsonDocument controllerResp;
	String sResp;

	Rotator->restoreDefaultMotorSettings();
	controllerResp["value"] = "Restored";
	serializeJson(controllerResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void shutterSpeedValue(Request &req, Response &res)
{
	JsonDocument controllerResp;
	String sResp;

	if(req.method() == Request::PUT) {
		JsonDocument FormData;
		formDataToJson(req, FormData);
		if(FormData.size()==0){
		}
		else {
			if(FormData["value"].is<long>()) {
				String shutterMessage;
				String sTmpString = String(SPEED_SHUTTER);
				RemoteShutter.speed = FormData["value"];
				shutterMessage = sTmpString + String(RemoteShutter.speed);
				shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
				ReceiveWiFi(shutterClient);
			}
		}
	}

	controllerResp["value"] = RemoteShutter.speed;
	serializeJson(controllerResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void shutterAccelerationValue(Request &req, Response &res)
{
	JsonDocument controllerResp;
	String sResp;

	if(req.method() == Request::PUT) {
		JsonDocument FormData;
		formDataToJson(req, FormData);
		if(FormData.size()==0){
		}
		else {
			if(FormData["value"].is<long>()) {
				String shutterMessage;
				String sTmpString = String(ACCELERATION_SHUTTER);
				RemoteShutter.acceleration = FormData["value"];
				shutterMessage = sTmpString + String(RemoteShutter.acceleration);
				shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
				ReceiveWiFi(shutterClient);
			}
		}
	}

	controllerResp["value"] = RemoteShutter.acceleration;
	serializeJson(controllerResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void restoreShutterMotorValues(Request &req, Response &res)
{
	JsonDocument controllerResp;
	String sResp;

	if(req.method() == Request::PUT) {
		JsonDocument FormData;

		String shutterMessage;
		String sTmpString = String(SHUTTER_RESTORE_MOTOR_DEFAULT);
		shutterMessage = sTmpString+ "#";
		shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
		ReceiveWiFi(shutterClient);
		shutterMessage = String(SPEED_SHUTTER)+ "#";
		shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
		ReceiveWiFi(shutterClient);
		shutterMessage = String(ACCELERATION_SHUTTER)+ "#";
		shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
		ReceiveWiFi(shutterClient);
	}

	controllerResp["value"] = "Restored";
	serializeJson(controllerResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void shutterWatchdogTimerValue(Request &req, Response &res)
{
	JsonDocument controllerResp;
	String sResp;

	if(req.method() == Request::PUT) {
		JsonDocument FormData;
		formDataToJson(req, FormData);
		if(FormData.size()==0){
		}
		else {
			if(FormData["value"].is<long>()) {
				String shutterMessage;
				String sTmpString = String(WATCHDOG_INTERVAL);
				RemoteShutter.watchdogInterval = FormData["value"];
				shutterMessage = sTmpString + String(RemoteShutter.watchdogInterval);
				shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
				ReceiveWiFi(shutterClient);
			}
		}
	}

	controllerResp["value"] = RemoteShutter.watchdogInterval;
	serializeJson(controllerResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void domeVoltageCutoffValue(Request &req, Response &res)
{
	JsonDocument controllerResp;
	String sResp;

	if(req.method() == Request::PUT) {
		JsonDocument FormData;
		formDataToJson(req, FormData);
		if(FormData.size()==0){
		}
		else {
			if(FormData["value"].is<long>()) {
				Rotator->SetLowVoltageCutoff(FormData["value"]);
			}
		}
	}

	controllerResp["value"] = Rotator->GetVoltString();
	serializeJson(controllerResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void shutterVoltageCutoffValue(Request &req, Response &res)
{
	JsonDocument controllerResp;
	String sResp;

	if(req.method() == Request::PUT) {
		JsonDocument FormData;
		formDataToJson(req, FormData);
		if(FormData.size()==0){
		}
		else {
			if(FormData["value"].is<long>()) {
				String shutterMessage;
				String sTmpString = String(VOLTS_SHUTTER);
				RemoteShutter.voltsCutOff = FormData["value"];
				shutterMessage = sTmpString + String(RemoteShutter.voltsCutOff);
				shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
				ReceiveWiFi(shutterClient);
			}
		}
	}

	controllerResp["value"] = RemoteShutter.voltsCutOff;
	serializeJson(controllerResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void unsafeDomeAction(Request &req, Response &res)
{
	JsonDocument controllerResp;
	String sResp;

	if(req.method() == Request::PUT) {
		JsonDocument FormData;
		formDataToJson(req, FormData);
		if(FormData.size()==0){
		}
		else {
			if(FormData["value"].is<long>()) {
				Rotator->SetConditionsAction(FormData["value"]);
			}
		}
	}

	controllerResp["value"] = Rotator->GetConditionsAction();
	serializeJson(controllerResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}


void envConditionState(Request &req, Response &res)
{
	JsonDocument controllerResp;
	String sResp;

	controllerResp["value"] = bool(bIsSafe);
	serializeJson(controllerResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

//
// Alpaca server class
//
class DomeAlpacaServer
{
public :
	DomeAlpacaServer(int port=ALPACA_SERVER_PORT);
	void startServer();
	void checkForRequest();


private :
	EthernetServer *mRestServer;
	Application  *m_AlpacaRestServer;
	int m_nRestPort;
};

DomeAlpacaServer::DomeAlpacaServer(int port)
{
	m_nRestPort = port;
	mRestServer = nullptr;
	m_AlpacaRestServer = nullptr;
	nTransactionID = 0;
}

void DomeAlpacaServer::startServer()
{
	mRestServer = new EthernetServer(m_nRestPort);
	m_AlpacaRestServer = new Application();

	DBPrintln("m_AlpacaRestServer starting");
	DBPrintln("m_AlpacaRestServer UUID : " + String(uuid.toCharArray()));
	mRestServer->begin();

	sRedirectURL = String("http://")+ sLocalIPAdress + String(":") + String(ALPACA_SERVER_PORT) + String("/setup/v1/dome/0/setup");
	DBPrintln("Redirect URL for setup : " + sRedirectURL);

	DBPrintln("m_AlpacaRestServer mapping endpoints");

	m_AlpacaRestServer->use("/", &redirectToSetup);
	m_AlpacaRestServer->use("/setup", &redirectToSetup);

	m_AlpacaRestServer->get("/management/apiversions", &getApiVersion);
	m_AlpacaRestServer->get("/management/v1/configureddevices", &getConfiguredDevice);
	m_AlpacaRestServer->get("/management/v1/description", &getDescription);

	m_AlpacaRestServer->use("/setup/v1/dome/0/setup", &doSetup);

	m_AlpacaRestServer->put("/api/v1/dome/0/action", &doAction);
	m_AlpacaRestServer->put("/api/v1/dome/0/commandblind", &doCommandBlind);
	m_AlpacaRestServer->put("/api/v1/dome/0/commandbool", &doCommandBool);
	m_AlpacaRestServer->put("/api/v1/dome/0/commandstring", &doCommandString);

	m_AlpacaRestServer->get("/api/v1/dome/0/connected", &getConnected);
	m_AlpacaRestServer->put("/api/v1/dome/0/connected", &setConnected);

	// platform 7
	m_AlpacaRestServer->put("/api/v1/dome/0/connect", &domeConnect);
	m_AlpacaRestServer->get("/api/v1/dome/0/connecting", &domeConnecting);
	m_AlpacaRestServer->put("/api/v1/dome/0/disconnect", &domeDisconnect);
	m_AlpacaRestServer->get("/api/v1/dome/0/devicestate", &getDomeState);

	m_AlpacaRestServer->get("/api/v1/dome/0/description", &getDeviceDescription);
	m_AlpacaRestServer->get("/api/v1/dome/0/driverinfo", &getDriverInfo);
	m_AlpacaRestServer->get("/api/v1/dome/0/driverversion", &getDriverVersion);
	m_AlpacaRestServer->get("/api/v1/dome/0/interfaceversion", &getInterfaceVersion);
	m_AlpacaRestServer->get("/api/v1/dome/0/name", &getName);
	m_AlpacaRestServer->get("/api/v1/dome/0/supportedactions", &getSupportedActions);
	m_AlpacaRestServer->get("/api/v1/dome/0/altitude", &getAltitude);
	m_AlpacaRestServer->get("/api/v1/dome/0/athome", &geAtHome);
	m_AlpacaRestServer->get("/api/v1/dome/0/atpark", &geAtPark);
	m_AlpacaRestServer->get("/api/v1/dome/0/azimuth", &getAzimuth);
	m_AlpacaRestServer->get("/api/v1/dome/0/canfindhome", &canfindhome);
	m_AlpacaRestServer->get("/api/v1/dome/0/canpark", &canPark);
	m_AlpacaRestServer->get("/api/v1/dome/0/cansetaltitude", &canSetAltitude);
	m_AlpacaRestServer->get("/api/v1/dome/0/cansetazimuth", &canSetAzimuth);
	m_AlpacaRestServer->get("/api/v1/dome/0/cansetpark", &canSetPark);
	m_AlpacaRestServer->get("/api/v1/dome/0/cansetshutter", &canSetShutter);
	m_AlpacaRestServer->get("/api/v1/dome/0/canslave", &canSlave);
	m_AlpacaRestServer->get("/api/v1/dome/0/cansyncazimuth", &canSyncAzimuth);
	m_AlpacaRestServer->get("/api/v1/dome/0/shutterstatus", &getShutterStatus);

	m_AlpacaRestServer->get("/api/v1/dome/0/slaved", &getSlaved);
	m_AlpacaRestServer->put("/api/v1/dome/0/slaved", &setSlaved);

	m_AlpacaRestServer->get("/api/v1/dome/0/slewing", &getSlewing);

	m_AlpacaRestServer->put("/api/v1/dome/0/abortslew", &doAbort);
	m_AlpacaRestServer->put("/api/v1/dome/0/closeshutter", &doCloseShutter);
	m_AlpacaRestServer->put("/api/v1/dome/0/findhome", &doFindHome);
	m_AlpacaRestServer->put("/api/v1/dome/0/openshutter", &doOpenShutter);
	m_AlpacaRestServer->put("/api/v1/dome/0/park", &doPark);
	m_AlpacaRestServer->put("/api/v1/dome/0/setpark", &setPark);
	m_AlpacaRestServer->put("/api/v1/dome/0/slewtoaltitude", &doAltitudeSlew);
	m_AlpacaRestServer->put("/api/v1/dome/0/slewtoazimuth", &doGoTo);
	m_AlpacaRestServer->put("/api/v1/dome/0/synctoazimuth", &doSyncAzimuth);

	// adding our own endpoints for the settings
	m_AlpacaRestServer->use("/setup/homePosition", &homePosition);
	m_AlpacaRestServer->use("/setup/parkPosition", &parkPosition);

	m_AlpacaRestServer->use("/setup/reverseDirection", &reverseDirectionState);
	m_AlpacaRestServer->use("/setup/shutterOpenOrder", &shutterOpenOrderValue);

	m_AlpacaRestServer->use("/setup/wifiSSID", &wifiSSIDValue);
	m_AlpacaRestServer->get("/setup/shutterPresentState", &isShutterPresentState);


	m_AlpacaRestServer->use("/setup/useDHCP", &useDHCPState);

	m_AlpacaRestServer->get("/setup/macAddress", &macAddressValue);
	m_AlpacaRestServer->use("/setup/ipAddress", &ipAddressValue);
	m_AlpacaRestServer->use("/setup/subnetMask", &subnetMaskValue);
	m_AlpacaRestServer->use("/setup/ipGeteway", &ipGetewayValue);


	m_AlpacaRestServer->use("/setup/domeCalibrate", &domeCalibrateAction);

	m_AlpacaRestServer->use("/setup/stepPerRevolution", &stepPerRevolutionValue);
	m_AlpacaRestServer->use("/setup/rotationSpeed", &rotationSpeedValue);
	m_AlpacaRestServer->use("/setup/rotationAcceleration", &rotationAccelerationValue);
	m_AlpacaRestServer->put("/setup/restoreRotationMotorSettings", &restoreRotationMotorValues);

	m_AlpacaRestServer->use("/setup/shutterSpeed", &shutterSpeedValue);
	m_AlpacaRestServer->use("/setup/shutterAcceleration", &shutterAccelerationValue);
	m_AlpacaRestServer->put("/setup/restoreShutterMotorSettings", &restoreShutterMotorValues);

	m_AlpacaRestServer->use("/setup/shutterWatchdogTimerValue", &shutterWatchdogTimerValue);
	m_AlpacaRestServer->use("/setup/domeVoltageCutoff", &domeVoltageCutoffValue);
	m_AlpacaRestServer->use("/setup/shutterVoltageCutoff", &shutterVoltageCutoffValue);
	m_AlpacaRestServer->use("/setup/unsafeDomeAction", &unsafeDomeAction);

	m_AlpacaRestServer->get("/setup/domeVoltage", &domeVoltageCutoffValue);
	m_AlpacaRestServer->get("/setup/shutterVoltage", &shutterVoltageCutoffValue);
	m_AlpacaRestServer->get("/setup/envCondition", &envConditionState);

	DBPrintln("m_AlpacaRestServer started");
}


void DomeAlpacaServer::checkForRequest()
{
	// process incoming connections one at a time
	EthernetClient client = mRestServer->accept();
	if (client.connected()) {
		m_AlpacaRestServer->process(&client);
		client.stop();
		nTransactionID++;
  }
}
