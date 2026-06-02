// Alpaca API functions
//
//  Created by Rodolphe Pineau on 2024/04/16
//  Copyright © 2024 Rodolphe Pineau. All rights reserved.
//

#pragma message "Alpaca server enabled"
#include <vector>
#include <functional>
#include <Network.h>
#include <ArduinoJson.h>
// Alpaca REST server
#include <UUID.h>
#include <aWOT.h>

#include "dome_controller_html.h"
// test
// #include <LittleFS.h>

#define ALPACA_DISCOVERY_PORT 32227
#define ALPACA_SERVER_PORT 80
#define ALPACA_VAR_BUF_LEN 256
#define ALPACA_OK 0
#define DISCOVERY_ERROR -1
#define DOME_INTERFACE_VERSION 3
#define UDP_MAX_DATA_SIZE 64

enum ShutterStates { OPEN, CLOSED, OPENING, CLOSING, BOTTOM_OPEN, BOTTOM_CLOSED, BOTTOM_OPENING, BOTTOM_CLOSING, ERROR, FINISHING_OPEN, FINISHING_CLOSE };
enum AlpacaShutterStates { A_OPEN=0, A_CLOSED, A_OPENING, A_CLOSING,  A_ERROR};
enum domeState {MOVING, HOMING, PARKING, IDLE};
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
	NetworkUDP *discoveryServer;
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
	discoveryServer = new NetworkUDP();
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

	char packetBuffer[UDP_MAX_DATA_SIZE+1];
	int packetSize = discoveryServer->parsePacket();
	if (packetSize) {
		DBPrintln("Alpaca discovery server request");
		memset(packetBuffer,0,sizeof(packetBuffer));
		if(packetSize > UDP_MAX_DATA_SIZE)
			packetSize = UDP_MAX_DATA_SIZE;
		discoveryServer->read(packetBuffer, packetSize);
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
		discoveryServer->write((uint8_t *)sDiscoveryResponse.c_str(), sDiscoveryResponse.length());
		discoveryServer->endPacket();
	}
	return ALPACA_OK;
}


int getAlpacaShutterState()
{
	int nAlpacaShutterState = A_ERROR;
	String sTmpString;
#ifdef USE_WIFI
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
#endif
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

		DBPrintln(String(__func__) + " : name :'" + String(sName) + "' with value : '" + String(sValue) + "'");

		if(isDigit(value[0])) {
			if(sValue.indexOf('.') == -1) {
				// int
				FormData[sName] = sValue.toInt();
			} else {
				// check if it could be an IP (more than one dot)
				int dotCount = 0;
				for(char c : sValue) if(c == '.') dotCount++;
				if(dotCount > 1) {
					// IP address or similar — treat as string
					FormData[sName] = sValue;
				} else {
					// float
					FormData[sName] = sValue.toFloat();
				}
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

bool getIDs(Request &req, JsonDocument &jsonResp, JsonDocument &FormData)
{
	char ClientID[64];
	char ClientTransactionID[64];
	String sClientId;
	String sClientTransactionId;
	std::vector<std::vector<String>> svParameters;

	bool bParamOk = true;

	DBPrintln("getIDs");

	jsonResp["ServerTransactionID"] = nTransactionID;

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
			jsonResp["ClientID"] = sClientId.toInt()<0?0:sClientId.toInt();
		if(sClientTransactionId.length())
			jsonResp["ClientTransactionID"] = sClientTransactionId.toInt()<0?0:sClientTransactionId.toInt();
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
				jsonResp["ClientID"] = sClientId.toInt()<0?0:sClientId.toInt();

			}
			if(FormData["clienttransactionid"].is<unsigned long>()) {
				serializeJson(FormData["clienttransactionid"], sClientTransactionId);
				sClientTransactionId.trim();
				jsonResp["ClientTransactionID"] = sClientTransactionId.toInt()<0?0:sClientTransactionId.toInt();
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

void AlpacaError_x400(JsonDocument jsonResp, Response &res, String errMsg="Not Implemented")
{
	String sResp;
	jsonResp["ErrorNumber"] = 0x400;
	jsonResp["ErrorMessage"] = errMsg;
	jsonResp["Value"] = false;
	serializeJson(jsonResp, sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void AlpacaError_x401(JsonDocument &jsonResp, Response &res, String errMsg="Invalid parameters")
{
	String sResp;
	jsonResp["ErrorNumber"] = 0x401;
	jsonResp["ErrorMessage"] = errMsg;
	serializeJson(jsonResp, sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}


void AlpacaError_x408(JsonDocument &jsonResp, Response &res, String errMsg="")
{
	String sResp;
	jsonResp["ErrorNumber"] = 0x408;
	jsonResp["ErrorMessage"] = errMsg;
	serializeJson(jsonResp, sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void redirectToSetup(Request &req, Response &res)
{
	res.set("Location", sRedirectURL.c_str());
    res.sendStatus(302);
}

void getApiVersion(Request &req, Response &res)
{
	JsonDocument jsonResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** getApiVersion ********** ]");
	bParamsOk = getIDs(req, jsonResp, FormData);


	jsonResp["Value"][0] = 1;

	serializeJson(jsonResp, sResp);

	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void getDescription(Request &req, Response &res)
{
	JsonDocument jsonResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** getDescription ********** ]");
	bParamsOk = getIDs(req, jsonResp, FormData);

	jsonResp["Value"]["ServerName"]= "RTIDome Alpaca";
	jsonResp["Value"]["Manufacturer"]= "RTI-Zone";
	jsonResp["Value"]["ManufacturerVersion"]= VERSION;
	jsonResp["Value"]["Location"]= "Earth";

	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void getConfiguredDevice(Request &req, Response &res)
{
	JsonDocument jsonResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** getConfiguredDevice ********** ]");
	bParamsOk = getIDs(req, jsonResp, FormData);

	jsonResp["Value"][0] ["DeviceName"]= "RTIDome";
	jsonResp["Value"][0] ["DeviceType"]= "dome";
	jsonResp["Value"][0] ["DeviceNumber"]= 0;
	jsonResp["Value"][0] ["UniqueID"]= uuid;

	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void doAction(Request &req, Response &res)
{
	JsonDocument jsonResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;
	String sAction;
	String sParameters;

	DBPrintln("[ ********** doAction ********** ]");
	bParamsOk = getIDs(req, jsonResp, FormData);

	if(!bParamsOk){
		AlpacaError_x401(jsonResp, res);
		return;
	}

	serializeJson(FormData["action"], sAction);
	serializeJson(FormData["parameters"], sParameters);
#ifdef DEBUG
	DBPrintln("sAction : " + sAction);
	DBPrintln("sParameters : " + sParameters);
#endif

	jsonResp["ErrorNumber"] = 0;
	jsonResp["ErrorMessage"] = "";
	jsonResp["Value"] = "Ok";
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void doCommandBlind(Request &req, Response &res)
{
	JsonDocument jsonResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;
	String sClientId;
	String sClientTransactionId;

	DBPrintln("[ ********** doCommandBlind ********** ]");
	bParamsOk = getIDs(req, jsonResp, FormData);

	if(!bParamsOk){
		AlpacaError_x401(jsonResp, res);
		return;
	}

	jsonResp["ErrorNumber"] = 0;
	jsonResp["ErrorMessage"] = "";
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void doCommandBool(Request &req, Response &res)
{
	JsonDocument jsonResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;
	String sClientId;
	String sClientTransactionId;

	DBPrintln("[ ********** doCommandBool ********** ]");
	bParamsOk = getIDs(req, jsonResp, FormData);


	if(!bParamsOk){
		AlpacaError_x401(jsonResp, res);
		return;
	}

	jsonResp["ErrorNumber"] = 0;
	jsonResp["ErrorMessage"] = "";
	jsonResp["Value"] = true;
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void doCommandString(Request &req, Response &res)
{
	JsonDocument jsonResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;
	String sClientId;
	String sClientTransactionId;

	DBPrintln("[ ********** doCommandString ********** ]");
	bParamsOk = getIDs(req, jsonResp, FormData);


	if(!bParamsOk){
		AlpacaError_x401(jsonResp, res);
		return;
	}

	jsonResp["ErrorNumber"] = 0;
	jsonResp["ErrorMessage"] = "";
	jsonResp["Value"] = "Ok";
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void getConnected(Request &req, Response &res)
{
	JsonDocument jsonResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** getConected ********** ]");
	bParamsOk = getIDs(req, jsonResp, FormData);

	jsonResp["ErrorNumber"] = 0;
	jsonResp["ErrorMessage"] = "";
	jsonResp["Value"] = bAlpacaConnected;
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void setConnected(Request &req, Response &res)
{
	JsonDocument jsonResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;
	String sClientId;
	String sClientTransactionId;
	String sParameter;
	String sTmp;

	DBPrintln("[ ********** setConected ********** ]");
	bParamsOk = getIDs(req, jsonResp, FormData);

	if(!bParamsOk){
		AlpacaError_x401(jsonResp, res);
		return;
	}

	if(!FormData["connected"].is<bool>()) {
		AlpacaError_x401(jsonResp, res);
		return;
	}

	bAlpacaConnected = FormData["connected"];
	DBPrintln("bAlpacaConnected : " + (bAlpacaConnected?String("true"):String("false")));

	jsonResp["ErrorNumber"] = 0;
	jsonResp["ErrorMessage"] = "";
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void domeConnect(Request &req, Response &res)
{
	JsonDocument jsonResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;
	String sClientId;
	String sClientTransactionId;
	String sParameter;
	String sTmp;

	DBPrintln("[ ********** setConected ********** ]");
	bParamsOk = getIDs(req, jsonResp, FormData);

	if(!bParamsOk){
		AlpacaError_x401(jsonResp, res);
		return;
	}

	bAlpacaConnected = true;
	DBPrintln("bAlpacaConnected : " + (bAlpacaConnected?String("true"):String("false")));

	jsonResp["ErrorNumber"] = 0;
	jsonResp["ErrorMessage"] = "";
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void domeConnecting(Request &req, Response &res)
{
	JsonDocument jsonResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;
	String sClientId;
	String sClientTransactionId;
	String sParameter;
	String sTmp;

	DBPrintln("[ ********** setConected ********** ]");
	bParamsOk = getIDs(req, jsonResp, FormData);

	if(!bParamsOk){
		AlpacaError_x401(jsonResp, res);
		return;
	}

	jsonResp["ErrorNumber"] = 0;
	jsonResp["ErrorMessage"] = "";
	jsonResp["Value"] = false; // it's already connected
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}



void getDomeState(Request &req, Response &res)
{
	JsonDocument jsonResp;
	JsonDocument jsTmp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;
	float Alt, Az;
	float dParkPos, dCurrentAz;
	bool bParked = false;

	DBPrintln("[ ********** getDomeState ********** ]");
	bParamsOk = getIDs(req, jsonResp, FormData);

	jsonResp["ErrorNumber"] = 0;
	jsonResp["ErrorMessage"] = "";

#ifdef USE_WIFI
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
	jsTmp["Name"] = "Altitude";
	jsTmp["Value"] = Alt;
	jsonResp["Value"].add(jsTmp);
	jsTmp.clear();
#endif

	jsTmp["Name"] = "AtHome";
	jsTmp["Value"] = (Rotator->GetHomeStatus() == ATHOME);
	jsonResp["Value"].add(jsTmp);
	jsTmp.clear();

	dParkPos = Rotator->GetParkAzimuth();
	dCurrentAz = Rotator->GetAzimuth();
	if(Rotator->checkBoundaries(dParkPos, dCurrentAz, 1.0)) {
		bParked = true;
	}
	jsTmp["Name"] = "AtPark";
	jsTmp["Value"] = bParked;
	jsonResp["Value"].add(jsTmp);
	jsTmp.clear();

	jsTmp["Name"] = "Azimuth";
	jsTmp["Value"] = Rotator->GetAzimuth();
	jsonResp["Value"].add(jsTmp);
	jsTmp.clear();

	jsTmp["Name"] = "ShutterStatus";
	jsTmp["Value"] = getAlpacaShutterState();
	jsonResp["Value"].add(jsTmp);
	jsTmp.clear();


	jsTmp["Name"] = "Slewing";
	if(Rotator->GetSeekMode() != NOT_MOVING) {
		jsTmp["Value"] = true;
	}
	else {
		jsTmp["Value"] = false;
	}
	jsonResp["Value"].add(jsTmp);

	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void domeDisconnect(Request &req, Response &res)
{
	JsonDocument jsonResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;
	String sClientId;
	String sClientTransactionId;
	String sParameter;
	String sTmp;

	DBPrintln("[ ********** setConected ********** ]");
	bParamsOk = getIDs(req, jsonResp, FormData);

	if(!bParamsOk){
		AlpacaError_x401(jsonResp, res);
		return;
	}

	bAlpacaConnected = false;
	DBPrintln("bAlpacaConnected : " + (bAlpacaConnected?String("true"):String("false")));

	jsonResp["ErrorNumber"] = 0;
	jsonResp["ErrorMessage"] = "";
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void getDeviceDescription(Request &req, Response &res)
{
	JsonDocument jsonResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** getDeviceDescription ********** ]");
	bParamsOk = getIDs(req, jsonResp, FormData);

	jsonResp["ErrorNumber"] = 0;
	jsonResp["ErrorMessage"] = "";
	jsonResp["Value"]= "RTI-Zone dome controller";
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void getDriverInfo(Request &req, Response &res)
{
	JsonDocument jsonResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** getDriverInfo ********** ]");
	bParamsOk = getIDs(req, jsonResp, FormData);

	jsonResp["ErrorNumber"] = 0;
	jsonResp["ErrorMessage"] = "";
	jsonResp["Value"]= "RTI-Zone Dome controller";
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void getDriverVersion(Request &req, Response &res)
{
	JsonDocument jsonResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** getDriverVersion ********** ]");
	bParamsOk = getIDs(req, jsonResp, FormData);


	jsonResp["ErrorNumber"] = 0;
	jsonResp["ErrorMessage"] = "";
	jsonResp["Value"]= String(VERSION);
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void getInterfaceVersion(Request &req, Response &res)
{
	JsonDocument jsonResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** getInterfaceVersion ********** ]");
	bParamsOk = getIDs(req, jsonResp, FormData);

	jsonResp["ErrorNumber"] = 0;
	jsonResp["ErrorMessage"] = "";
	jsonResp["Value"]= DOME_INTERFACE_VERSION;
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void getName(Request &req, Response &res)
{
	JsonDocument jsonResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** getName ********** ]");
	bParamsOk = getIDs(req, jsonResp, FormData);

	jsonResp["ErrorNumber"] = 0;
	jsonResp["ErrorMessage"] = "";
	jsonResp["Value"]= "RTI-Zone Dome controller";
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void getSupportedActions(Request &req, Response &res)
{
	JsonDocument jsonResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** getSupportedActions ********** ]");
	bParamsOk = getIDs(req, jsonResp, FormData);


	jsonResp["Value"] = "[]";
	jsonResp["ErrorNumber"] = 0;
	jsonResp["ErrorMessage"] = "";
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void getAltitude(Request &req, Response &res)
{
	JsonDocument jsonResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	String sTmpString = String(STATE_SHUTTER);

	DBPrintln("[ ********** getAltitude ********** ]");
	bParamsOk = getIDs(req, jsonResp, FormData);

	jsonResp["ErrorNumber"] = 0;
	jsonResp["ErrorMessage"] = "";

#ifdef USE_WIFI
	shutterClient.print(sTmpString + "#");
	ReceiveWiFi(shutterClient);
	switch (RemoteShutter.state ) {
		case OPEN:
			jsonResp["Value"] = 90.0f;
			break;
		case CLOSED:
			jsonResp["Value"] = 0.0f;
			break;
		default:
			jsonResp["Value"] = 0.0f;
			break;
	}
#else
	jsonResp["Value"] = 0.0f;
#endif
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());

}

void geAtHome(Request &req, Response &res)
{
	JsonDocument jsonResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** geAtHome ********** ]");
	bParamsOk = getIDs(req, jsonResp, FormData);

	jsonResp["ErrorNumber"] = 0;
	jsonResp["ErrorMessage"] = "";

	if(String(Rotator->GetHomeStatus() == ATHOME)) {
		jsonResp["Value"] = true;
	}
	else {
		jsonResp["Value"] = false;
	}
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());

}

void geAtPark(Request &req, Response &res)
{
	JsonDocument jsonResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** geAtPark ********** ]");
	bParamsOk = getIDs(req, jsonResp, FormData);

	if(bParked) {
		jsonResp["Value"] = true;
	}
	else {
		jsonResp["Value"] = false;
	}
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void getAzimuth(Request &req, Response &res)
{
	JsonDocument jsonResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** getAzimuth ********** ]");
	bParamsOk = getIDs(req, jsonResp, FormData);

	jsonResp["ErrorNumber"] = 0;
	jsonResp["ErrorMessage"] = "";

	jsonResp["Value"] = Rotator->GetAzimuth();

	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void canfindhome(Request &req, Response &res)
{
	JsonDocument jsonResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** canfindhome ********** ]");
	bParamsOk = getIDs(req, jsonResp, FormData);

	jsonResp["ErrorNumber"] = 0;
	jsonResp["ErrorMessage"] = "";

	jsonResp["Value"] = true;

	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void canPark(Request &req, Response &res)
{
	JsonDocument jsonResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** canPark ********** ]");
	bParamsOk = getIDs(req, jsonResp, FormData);

	jsonResp["ErrorNumber"] = 0;
	jsonResp["ErrorMessage"] = "";

	jsonResp["Value"] = true;

	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void canSetAltitude(Request &req, Response &res)
{
	JsonDocument jsonResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** canSetAltitude ********** ]");
	bParamsOk = getIDs(req, jsonResp, FormData);

	jsonResp["ErrorNumber"] = 0;
	jsonResp["ErrorMessage"] = "";

	jsonResp["Value"] = false;

	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void canSetAzimuth(Request &req, Response &res)
{
	JsonDocument jsonResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** canSetAzimuth ********** ]");
	bParamsOk = getIDs(req, jsonResp, FormData);

	jsonResp["ErrorNumber"] = 0;
	jsonResp["ErrorMessage"] = "";

	jsonResp["Value"] = true;

	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void canSetPark(Request &req, Response &res)
{
	JsonDocument jsonResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** canSetPark ********** ]");
	bParamsOk = getIDs(req, jsonResp, FormData);

	jsonResp["ErrorNumber"] = 0;
	jsonResp["ErrorMessage"] = "";

	jsonResp["Value"] = true;

	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void canSetShutter(Request &req, Response &res)
{
	JsonDocument jsonResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** canSetShutter ********** ]");
	bParamsOk = getIDs(req, jsonResp, FormData);

	jsonResp["ErrorNumber"] = 0;
	jsonResp["ErrorMessage"] = "";

#ifndef USE_WIFI
	jsonResp["Value"] = false;
#else
	if(!nbWiFiClient) {
		jsonResp["Value"] = true;
	}
	else {
		jsonResp["Value"] = true;
	}
#endif
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void canSlave(Request &req, Response &res)
{
	JsonDocument jsonResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** canSlave ********** ]");
	bParamsOk = getIDs(req, jsonResp, FormData);

	jsonResp["ErrorNumber"] = 0;
	jsonResp["ErrorMessage"] = "";

	jsonResp["Value"] = false;

	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void canSyncAzimuth(Request &req, Response &res)
{
	JsonDocument jsonResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** canSyncAzimuth ********** ]");
	bParamsOk = getIDs(req, jsonResp, FormData);

	jsonResp["ErrorNumber"] = 0;
	jsonResp["ErrorMessage"] = "";

	jsonResp["Value"] = true;

	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void getShutterStatus(Request &req, Response &res)
{
	JsonDocument jsonResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	String sTmpString = String(STATE_SHUTTER);

	DBPrintln("[ ********** getShutterStatus ********** ]");
	bParamsOk = getIDs(req, jsonResp, FormData);

#ifndef USE_WIFI
	AlpacaError_x400(jsonResp, res);
	return;
#else
	if(!nbWiFiClient) {
		AlpacaError_x400(jsonResp, res, "Shutter not connected");
		return;
	}
	else {
		jsonResp["ErrorNumber"] = 0;
		jsonResp["ErrorMessage"] = "";
		shutterClient.print(sTmpString + "#");
		ReceiveWiFi(shutterClient);
		jsonResp["Value"] = getAlpacaShutterState();
	}
#endif
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void getSlaved(Request &req, Response &res)
{
	JsonDocument jsonResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** canSlave ********** ]");
	bParamsOk = getIDs(req, jsonResp, FormData);

	jsonResp["ErrorNumber"] = 0;
	jsonResp["ErrorMessage"] = "";

	jsonResp["Value"] = false;

	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void setSlaved(Request &req, Response &res)
{
	JsonDocument jsonResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** Slaved ********** ]");
	bParamsOk = getIDs(req, jsonResp, FormData);
	AlpacaError_x400(jsonResp, res, "Invalid parameters, missing 'Connected'");
}

void getSlewing(Request &req, Response &res)
{
	JsonDocument jsonResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** getSlewing ********** ]");
	bParamsOk = getIDs(req, jsonResp, FormData);

	jsonResp["ErrorNumber"] = 0;
	jsonResp["ErrorMessage"] = "";

	DBPrintln("Seekmode : " + String(Rotator->GetSeekMode()));

	if(Rotator->GetSeekMode() != NOT_MOVING) {
		jsonResp["Value"] = true;
	}
	else {
		jsonResp["Value"] = false;
	}

	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void doAbort(Request &req, Response &res)
{
	JsonDocument jsonResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** doAbort ********** ]");
	bParamsOk = getIDs(req, jsonResp, FormData);

	if(!bParamsOk){
		AlpacaError_x401(jsonResp, res);
		return;
	}

	jsonResp["ErrorNumber"] = 0;
	jsonResp["ErrorMessage"] = "";

	Abort(); // this is in the RotatorEth-esp32.ino

	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void doCloseShutter(Request &req, Response &res)
{
	JsonDocument jsonResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	String sTmpString = String(CLOSE_SHUTTER);

	DBPrintln("[ ********** doCloseShutter ********** ]");
	bParamsOk = getIDs(req, jsonResp, FormData);

	if(!bParamsOk){
		AlpacaError_x401(jsonResp, res);
		return;
	}

#ifndef USE_WIFI
	AlpacaError_x400(jsonResp, res);
	return;
#else
	if(!nbWiFiClient) {
		AlpacaError_x400(jsonResp, res, "Shutter not connected");
		return;
	} else {
		jsonResp["ErrorNumber"] = 0;
		jsonResp["ErrorMessage"] = "";
		shutterClient.print(sTmpString+ "#");
		ReceiveWiFi(shutterClient);
	}
#endif
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void doFindHome(Request &req, Response &res)
{
	JsonDocument jsonResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** doFindHome ********** ]");
	bParamsOk = getIDs(req, jsonResp, FormData);


	if(!bParamsOk){
		AlpacaError_x401(jsonResp, res);
		return;
	}

	if(bLowShutterVoltage) {
		AlpacaError_x408(jsonResp, res);
		return;
	}

	jsonResp["ErrorNumber"] = 0;
	jsonResp["ErrorMessage"] = "";

	Rotator->StartHoming();

	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void doOpenShutter(Request &req, Response &res)
{
	JsonDocument jsonResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	String sTmpString = String(OPEN_SHUTTER);

	DBPrintln("[ ********** doOpenShutter ********** ]");
	bParamsOk = getIDs(req, jsonResp, FormData);

	if(!bParamsOk){
		AlpacaError_x401(jsonResp, res);
		return;
	}

#ifndef USE_WIFI
	AlpacaError_x400(jsonResp, res);
	return;
#else
	if(!nbWiFiClient) {
		AlpacaError_x400(jsonResp, res, "Shutter not connected");
		return;
	}
	else if(bLowShutterVoltage) {
		AlpacaError_x408(jsonResp, res);
		return;
		}
	else {
		jsonResp["ErrorNumber"] = 0;
		jsonResp["ErrorMessage"] = "";

		shutterClient.print(sTmpString+ "#");
		ReceiveWiFi(shutterClient);
	}
#endif
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void doPark(Request &req, Response &res)
{
	JsonDocument jsonResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;
	float fParkPos;

	DBPrintln("[ ********** doPark ********** ]");
	bParamsOk = getIDs(req, jsonResp, FormData);

	if(!bParamsOk){
		AlpacaError_x401(jsonResp, res);
		return;
	}

	fParkPos = Rotator->GetParkAzimuth();
	Rotator->GoToAzimuth(fParkPos);
	bParked = true;

	jsonResp["ErrorNumber"] = 0;
	jsonResp["ErrorMessage"] = "";
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void setPark(Request &req, Response &res)
{
	JsonDocument jsonResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;
	float fParkPos;

	DBPrintln("[ ********** setPark ********** ]");
	bParamsOk = getIDs(req, jsonResp, FormData);

	if(!bParamsOk){
		AlpacaError_x401(jsonResp, res);
		return;
	}


	jsonResp["ErrorNumber"] = 0;
	jsonResp["ErrorMessage"] = "";
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
	fParkPos = Rotator->GetAzimuth();
	Rotator->SetParkAzimuth(fParkPos);
}

void doAltitudeSlew(Request &req, Response &res)
{
	JsonDocument jsonResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** doAltitudeSlew ********** ]");
	bParamsOk = getIDs(req, jsonResp, FormData);

#ifndef USE_WIFI
	if(!bParamsOk){
		AlpacaError_x401(jsonResp, res);
		return;
	}

	if(!FormData["altitude"].is<float>()) {
		AlpacaError_x401(jsonResp, res);
		return;
	}
	// in case we implement this one day.
	AlpacaError_x400(jsonResp, res);
	return;
#else
	AlpacaError_x401(jsonResp, res);
	return;
#endif

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());

}

void doGoTo(Request &req, Response &res)
{
	JsonDocument jsonResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;
	float dNewPos;

	DBPrintln("[ ********** doGoTo ********** ]");
	bParamsOk = getIDs(req, jsonResp, FormData);

	if(!bParamsOk){
		AlpacaError_x401(jsonResp, res);
		return;
	}

	if(bLowShutterVoltage) {
		AlpacaError_x408(jsonResp, res);
		return;
	}

	if(!FormData["azimuth"].is<float>()) {
		AlpacaError_x401(jsonResp, res);
		return;
	}

	dNewPos = FormData["azimuth"];
	if(dNewPos < 0.0f || dNewPos > 360.0f) {
		AlpacaError_x401(jsonResp, res, "Invalid azimuth");
		return;
	}

	Rotator->GoToAzimuth(dNewPos);

	jsonResp["ErrorNumber"] = 0;
	jsonResp["ErrorMessage"] = "";
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void doSyncAzimuth(Request &req, Response &res)
{
	JsonDocument jsonResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;
	float dNewPos;

	DBPrintln("[ ********** doSyncAzimuth ********** ]");
	bParamsOk = getIDs(req, jsonResp, FormData);

	if(!bParamsOk){
		AlpacaError_x401(jsonResp, res);
		return;
	}

	if(!FormData["azimuth"].is<float>()) {
		AlpacaError_x401(jsonResp, res, "Invalid azimuth");
		return;
	}

	dNewPos = FormData["azimuth"];
	if(dNewPos < 0.0f || dNewPos > 360.0f) {
		AlpacaError_x401(jsonResp, res);
		return;
	}

	Rotator->SyncPosition(dNewPos);

	jsonResp["ErrorNumber"] = 0;
	jsonResp["ErrorMessage"] = "";
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}


void doSetup(Request &req, Response &res)
{
	JsonDocument jsonResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;
	String sHTML;

	res.set("Content-Type", "text/html");

	DBPrintln("[ ********** doSetup ********** ]");
	bParamsOk = getIDs(req, jsonResp, FormData);
	res.print(DOME_CONTROLLER_HTML);
	res.print(sHTML);
}

//
// controller settings API
//
void homePosition(Request &req, Response &res)
{
	JsonDocument jsonResp;
	String sResp;
	float dPosition = 0.0f;

	if(req.method() == Request::PUT) {
		JsonDocument FormData;
		formDataToJson(req, FormData);
		if(FormData.size()==0){
			AlpacaError_x401(jsonResp, res);
			return;
		}
		else {
			if(FormData["value"].is<float>()) {
				dPosition = FormData["value"];
				Rotator->SetHomeAzimuth(dPosition);
			}
		}
	}

	jsonResp["value"] = Rotator->GetHomeAzimuth();
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void parkPosition(Request &req, Response &res)
{
	JsonDocument jsonResp;
	String sResp;
	float dPosition = 0.0f;

	if(req.method() == Request::PUT) {
		JsonDocument FormData;
		formDataToJson(req, FormData);
		if(FormData.size()==0){
			AlpacaError_x401(jsonResp, res);
			return;
		}
		else {
			if(FormData["value"].is<float>()) {
				dPosition = FormData["value"];
				Rotator->SetParkAzimuth(dPosition);
			}
		}
	}

	jsonResp["value"] = Rotator->GetParkAzimuth();
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void reverseDirectionState(Request &req, Response &res)
{
	JsonDocument jsonResp;
	String sResp;
	bool bReversed = false;
	if(req.method() == Request::PUT) {
		JsonDocument FormData;
		formDataToJson(req, FormData);
		if(FormData.size()==0){
			AlpacaError_x401(jsonResp, res);
			return;
		}
		else {
			if(FormData["value"].is<bool>()) {
				bReversed = FormData["value"];
				Rotator->SetReversed(bReversed);
			}
		}
	}

	jsonResp["value"] = Rotator->GetReversed();
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}


void shutterOpenOrderValue(Request &req, Response &res)
{
	JsonDocument jsonResp;
	String sResp;
	bool bBottomfirst = false;

	if(req.method() == Request::PUT) {
		JsonDocument FormData;
		formDataToJson(req, FormData);
		if(FormData.size()==0){
			AlpacaError_x401(jsonResp, res);
			return;
		}
		else {
			if(FormData["value"].is<int>()) {
				bBottomfirst = FormData["value"];
				// need implementation
				// Rotator->GetOpenOrder(bBottomfirst);
			}
		}
	}
	// need implementation
	// jsonResp["value"] = Rotator->GetOpenOrder();
	jsonResp["value"] = TOP_FIRST; // for now
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

#ifdef USE_WIFI

void wifiSSIDValue(Request &req, Response &res)
{
	JsonDocument jsonResp;
	String sResp;
	WIFIConfig l_WifiConfig;
	bool bReversed = false;
	String SSID;

	Rotator->getWiFiConfig(l_WifiConfig);

	if(req.method() == Request::PUT) {
		JsonDocument FormData;
		formDataToJson(req, FormData);
		if(FormData.size()==0){
			AlpacaError_x401(jsonResp, res);
			return;
		}
		else {
			if(FormData["value"].is<String>()) {
				l_WifiConfig.sSSID = FormData["value"].as<String>();
				// need implementation
				// Rotator->setWiFiConfig(l_WifiConfig);
				configureWiFi();

			}
		}
	}

	jsonResp["value"] = l_WifiConfig.sSSID;
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}
#endif

void isShutterPresentState(Request &req, Response &res)
{
	JsonDocument jsonResp;
	String sResp;
	bool bReversed = false;


	jsonResp["value"] = bool(bShutterPresent);
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}


void useDHCPState(Request &req, Response &res)
{
	JsonDocument jsonResp;
	String sResp;
	bool bUseDhcp = false;

	if(req.method() == Request::PUT) {
		JsonDocument FormData;
		formDataToJson(req, FormData);
		if(FormData.size()==0){
			AlpacaError_x401(jsonResp, res);
			return;
		}
		else {
			if(FormData["value"].is<bool>()) {
				bUseDhcp = FormData["value"];
				Rotator->setDHCPFlag(bUseDhcp);
			}
		}
	}

	jsonResp["value"] = Rotator->getDHCPFlag();
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void macAddressValue(Request &req, Response &res)
{
	JsonDocument jsonResp;
	String sResp;

	jsonResp["value"] = String(MAC_Address[0], HEX) + String(":") +
					String(MAC_Address[1], HEX) + String(":") +
					String(MAC_Address[2], HEX) + String(":") +
					String(MAC_Address[3], HEX) + String(":") +
					String(MAC_Address[4], HEX) + String(":") +
					String(MAC_Address[5], HEX);

	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());

}

void ipAddressValue(Request &req, Response &res)
{
	JsonDocument jsonResp;
	String sResp;

	if(req.method() == Request::PUT) {
		JsonDocument FormData;
		formDataToJson(req, FormData);
		if(FormData.size()==0){
			AlpacaError_x401(jsonResp, res);
			return;
		}
		else {
			if(FormData["value"].is<String>()) {
				Rotator->setIPAddress(FormData["value"]);
			}
		}
	}

	if(Rotator->getDHCPFlag()) {
		jsonResp["value"] = String(RotatorClass::IpAddress2String(domeEthernet.localIP()));
	}
	else {
		jsonResp["value"] = Rotator->getIPAddress();
	}
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void subnetMaskValue(Request &req, Response &res)
{
	JsonDocument jsonResp;
	String sResp;

	if(req.method() == Request::PUT) {
		JsonDocument FormData;
		formDataToJson(req, FormData);
		if(FormData.size()==0){
			AlpacaError_x401(jsonResp, res);
			return;
		}
		else {
			if(FormData["value"].is<String>()) {
				Rotator->setIPSubnetMask(FormData["value"]);
			}
		}
	}

	if(Rotator->getDHCPFlag()) {
		jsonResp["value"] = String(RotatorClass::IpAddress2String(domeEthernet.subnetMask()));
	}
	else {
		jsonResp["value"] = Rotator->getIPSubnetMask();
	}

	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void ipGatewayValue(Request &req, Response &res)
{
	JsonDocument jsonResp;
	String sResp;

	if(req.method() == Request::PUT) {
		JsonDocument FormData;
		formDataToJson(req, FormData);
		if(FormData.size()==0){
			AlpacaError_x401(jsonResp, res);
			return;
		}
		else {
			if(FormData["value"].is<String>()) {
				Rotator->setIPGateway(FormData["value"]);
			}
		}
	}

	if(Rotator->getDHCPFlag()) {
		jsonResp["value"] = String(RotatorClass::IpAddress2String(domeEthernet.gatewayIP()));
	}
	else {
		jsonResp["value"] = Rotator->getIPGateway();
	}

	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}


void restoreNetworkDefaults(Request &req, Response &res)
{
	JsonDocument jsonResp;
	String sResp;

	if(req.method() == Request::PUT) {
		Rotator->resetNetworkToDefaults();
	}

	jsonResp["value"] = "Restored";
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}


void domeCalibrateAction(Request &req, Response &res)
{
	JsonDocument jsonResp;
	String sResp;

	if(req.method() == Request::PUT) {
		JsonDocument FormData;
		formDataToJson(req, FormData);
		if(FormData.size()==0){
			AlpacaError_x401(jsonResp, res);
			return;
		}
		else {
			if(FormData["value"].is<String>()) {
				if(FormData["value"] == "start") {
					Rotator->StartCalibrating();
				}
				if(FormData["value"] == "stop") {
					Rotator->Stop();
				}
			}
		}
	}
	switch(Rotator->GetCalibrationState()) {
		case CALIBRATION_MOVE_OFF:
		case CALIBRATION_STEP1:
			jsonResp["value"] = CALIBRATION_STEP1;
			break;
		case CALIBRATION_MOVE_OFF2:
		case CALIBRATION_MEASURE:
			jsonResp["value"] = CALIBRATION_MEASURE;
			break;
		default:
			jsonResp["value"] = NOT_MOVING;
	}
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void stepPerRevolutionValue(Request &req, Response &res)
{
	JsonDocument jsonResp;
	String sResp;

	if(req.method() == Request::PUT) {
		JsonDocument FormData;
		formDataToJson(req, FormData);
		if(FormData.size()==0){
			AlpacaError_x401(jsonResp, res);
			return;
		}
		else {
			if(FormData["value"].is<long>()) {
				Rotator->SetStepsPerRotation(FormData["value"]);
			}
		}
	}

	jsonResp["value"] = Rotator->GetStepsPerRotation();
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void rotationSpeedValue(Request &req, Response &res)
{
	JsonDocument jsonResp;
	String sResp;

	if(req.method() == Request::PUT) {
		JsonDocument FormData;
		formDataToJson(req, FormData);
		if(FormData.size()==0){
			AlpacaError_x401(jsonResp, res);
			return;
		}
		else {
			if(FormData["value"].is<long>()) {
				Rotator->SetMaxSpeed(FormData["value"]);
			}
		}
	}

	jsonResp["value"] = Rotator->GetMaxSpeed();
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void rotationAccelerationValue(Request &req, Response &res)
{
	JsonDocument jsonResp;
	String sResp;

	if(req.method() == Request::PUT) {
		JsonDocument FormData;
		formDataToJson(req, FormData);
		if(FormData.size()==0){
			AlpacaError_x401(jsonResp, res);
			return;
		}
		else {
			if(FormData["value"].is<long>()) {
				Rotator->SetAcceleration(FormData["value"]);
			}
		}
	}

	jsonResp["value"] = Rotator->GetAcceleration();
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}


void restoreRotationMotorValues(Request &req, Response &res)
{
	JsonDocument jsonResp;
	String sResp;

	Rotator->restoreDefaultMotorSettings();
	jsonResp["value"] = "Restored";
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

#ifdef USE_WIFI
void shutterSpeedValue(Request &req, Response &res)
{
	JsonDocument jsonResp;
	String sResp;

	if(req.method() == Request::PUT) {
		JsonDocument FormData;
		formDataToJson(req, FormData);
		if(FormData.size()==0){
			AlpacaError_x401(jsonResp, res);
			return;
		}
		else {
			if(FormData["value"].is<long>()) {
				String shutterMessage;
				String sTmpString = String(SPEED_SHUTTER);
				RemoteShutter.speed = FormData["value"];
				shutterMessage = sTmpString + String(RemoteShutter.speed);
				shutterClient.write(shutterMessage.c_str(), shutterMessage.length());
				ReceiveWiFi(shutterClient);
			}
		}
	}

	jsonResp["value"] = RemoteShutter.speed;
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void shutterAccelerationValue(Request &req, Response &res)
{
	JsonDocument jsonResp;
	String sResp;

	if(req.method() == Request::PUT) {
		JsonDocument FormData;
		formDataToJson(req, FormData);
		if(FormData.size()==0){
			AlpacaError_x401(jsonResp, res);
			return;
		}
		else {
			if(FormData["value"].is<long>()) {
				String shutterMessage;
				String sTmpString = String(ACCELERATION_SHUTTER);
				RemoteShutter.acceleration = FormData["value"];
				shutterMessage = sTmpString + String(RemoteShutter.acceleration);
				shutterClient.write(shutterMessage.c_str(), shutterMessage.length());
				ReceiveWiFi(shutterClient);
			}
		}
	}

	jsonResp["value"] = RemoteShutter.acceleration;
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void restoreShutterMotorValues(Request &req, Response &res)
{
	JsonDocument jsonResp;
	String sResp;

	if(req.method() == Request::PUT) {
		JsonDocument FormData;

		String shutterMessage;
		String sTmpString = String(SHUTTER_RESTORE_MOTOR_DEFAULT);
		shutterMessage = sTmpString+ "#";
		shutterClient.write(shutterMessage.c_str(), shutterMessage.length());
		ReceiveWiFi(shutterClient);
		shutterMessage = String(SPEED_SHUTTER)+ "#";
		shutterClient.write(shutterMessage.c_str(), shutterMessage.length());
		ReceiveWiFi(shutterClient);
		shutterMessage = String(ACCELERATION_SHUTTER)+ "#";
		shutterClient.write(shutterMessage.c_str(), shutterMessage.length());
		ReceiveWiFi(shutterClient);
	}

	jsonResp["value"] = "Restored";
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void shutterWatchdogTimerValue(Request &req, Response &res)
{
	JsonDocument jsonResp;
	String sResp;

	if(req.method() == Request::PUT) {
		JsonDocument FormData;
		formDataToJson(req, FormData);
		if(FormData.size()==0){
			AlpacaError_x401(jsonResp, res);
			return;
		}
		else {
			if(FormData["value"].is<long>()) {
				String shutterMessage;
				String sTmpString = String(WATCHDOG_INTERVAL);
				RemoteShutter.watchdogInterval = FormData["value"];
				shutterMessage = sTmpString + String(RemoteShutter.watchdogInterval);
				shutterClient.write(shutterMessage.c_str(), shutterMessage.length());
				ReceiveWiFi(shutterClient);
			}
		}
	}

	jsonResp["value"] = RemoteShutter.watchdogInterval;
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void shutterVoltageCutoffValue(Request &req, Response &res)
{
	JsonDocument jsonResp;
	String sResp;

	if(req.method() == Request::PUT) {
		JsonDocument FormData;
		formDataToJson(req, FormData);
		if(FormData.size()==0){
			AlpacaError_x401(jsonResp, res);
			return;
		}
		else {
			if(FormData["value"].is<long>()) {
				String shutterMessage;
				String sTmpString = String(VOLTS_SHUTTER);
				RemoteShutter.voltsCutOff = FormData["value"];
				shutterMessage = sTmpString + String(RemoteShutter.voltsCutOff);
				shutterClient.write(shutterMessage.c_str(), shutterMessage.length());
				ReceiveWiFi(shutterClient);
			}
		}
	}

	jsonResp["value"] = RemoteShutter.voltsCutOff;
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

#endif

void unsafeDomeAction(Request &req, Response &res)
{
	JsonDocument jsonResp;
	String sResp;

	if(req.method() == Request::PUT) {
		JsonDocument FormData;
		formDataToJson(req, FormData);
		if(FormData.size()==0){
			AlpacaError_x401(jsonResp, res);
			return;
		}
		else {
			if(FormData["value"].is<long>()) {
				Rotator->SetConditionsAction(FormData["value"]);
			}
		}
	}
	switch(Rotator->GetConditionsAction()) {
		case DO_NOTHING:
			jsonResp["value"] = DO_NOTHING;
			break;
		case HOME:
			jsonResp["value"] = HOME;
			break;
		case PARK:
			jsonResp["value"] = PARK;
			break;
	}
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}


void envConditionState(Request &req, Response &res)
{
	JsonDocument jsonResp;
	String sResp;

	jsonResp["value"] = bool(bIsSafe);
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void homeDome(Request &req, Response &res)
{
	JsonDocument jsonResp;
	String sResp;

	Rotator->StartHoming();
	jsonResp["value"] = HOMING;
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void parkDome(Request &req, Response &res)
{
	JsonDocument jsonResp;
	String sResp;
	float fParkAz;

	fParkAz = Rotator->GetParkAzimuth();
	Rotator->GoToAzimuth(fParkAz);
	jsonResp["value"] = PARKING;
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}


void gotoAzimuth(Request &req, Response &res)
{
	JsonDocument jsonResp;
	String sResp;

	JsonDocument FormData;
	formDataToJson(req, FormData);
	if(FormData.size()==0){
		AlpacaError_x401(jsonResp, res);
		return;
	}
	else {
		if(FormData["value"].is<float>()) {
			float fTmp;
			fTmp = FormData["value"];
			while(fTmp < 0.0f) {
				fTmp += 360.0f;
			}
			while(fTmp > 360.0f) {
				fTmp -= 360.0f;
			}
			Rotator->GoToAzimuth(fTmp);
		}
	}
	jsonResp["value"] = MOVING;
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void getDomeAzimuth(Request &req, Response &res)
{
	JsonDocument jsonResp;
	String sResp;

	jsonResp["value"] = Rotator->GetAzimuth();
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}


void openShutter(Request &req, Response &res)
{
	JsonDocument jsonResp;
	String sResp;

	bOpenShutterButtonPressed = true;
	jsonResp["value"] = A_OPENING;
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void closeShutter(Request &req, Response &res)
{
	JsonDocument jsonResp;
	String sResp;
	float fParkAz;

	bCloseShutterButtonPressed = true;
	jsonResp["value"] = A_CLOSING;
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void getShutterState(Request &req, Response &res)
{
	JsonDocument jsonResp;
	String sResp;

	String sTmpString = String(STATE_SHUTTER);
	if(nbWiFiClient && shutterClient.connected()) {
		String shutterMessage = sTmpString+ "#";
		shutterClient.write(shutterMessage .c_str(), shutterMessage.length());
		vTaskDelay(DELAY_WIFI / portTICK_PERIOD_MS);
		ReceiveWiFi(shutterClient);
	}

	switch(RemoteShutter.state){
		case OPEN :
			jsonResp["value"] = A_OPEN;
			break;
		case CLOSED :
			jsonResp["value"] = A_CLOSED;
			break;
		case OPENING :
		case FINISHING_OPEN :
		case BOTTOM_OPENING :
			jsonResp["value"] = A_OPENING;
			break;
		case CLOSING :
		case FINISHING_CLOSE :
		case BOTTOM_CLOSING :
			jsonResp["value"] = A_CLOSING;
			break;
		case ERROR :
			jsonResp["value"] = A_ERROR;
			break;
		default:
			jsonResp["value"] = A_ERROR;
			break;
	}
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
}

void resetToFactory(Request &req, Response &res)
{
	JsonDocument jsonResp;
	String sResp;
	float fParkAz;

	jsonResp["value"] = "Resetting to factory";
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
	Rotator->resetAlltoDefault();
}

void uiAbort(Request &req, Response &res)
{
	JsonDocument jsonResp;
	String sResp;
	float fParkAz;

	jsonResp["value"] = "Aborting all motion";
	serializeJson(jsonResp, sResp);
	DBPrintln(String(__func__) + " : sResp : " + sResp);

	res.set("Content-Type", "application/json");
	res.write((uint8_t*)(sResp.c_str()),sResp.length());
	Abort();
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
	NetworkServer *mRestServer;
	Application  *m_AlpacaRestServer;
	int m_nRestPort;
};

DomeAlpacaServer::DomeAlpacaServer(int port)
{
	byte fuseMacForUUID[6];
	getFuseMac(fuseMacForUUID);
	m_nRestPort = port;
	mRestServer = nullptr;
	m_AlpacaRestServer = nullptr;
	nTransactionID = 0;
	uuid.seed(fuseMacForUUID[4],fuseMacForUUID[5]);
	uuid.generate();
}

void DomeAlpacaServer::startServer()
{
	mRestServer = new NetworkServer(m_nRestPort);
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

	m_AlpacaRestServer->use("/setup/useDHCP", &useDHCPState);
	m_AlpacaRestServer->get("/setup/macAddress", &macAddressValue);
	m_AlpacaRestServer->use("/setup/ipAddress", &ipAddressValue);
	m_AlpacaRestServer->use("/setup/subnetMask", &subnetMaskValue);
	m_AlpacaRestServer->use("/setup/ipGateway", &ipGatewayValue);
	m_AlpacaRestServer->use("/setup/restoreNetworkDefaults", &restoreNetworkDefaults);

	m_AlpacaRestServer->use("/setup/domeCalibrate", &domeCalibrateAction);
	m_AlpacaRestServer->use("/setup/stepPerRevolution", &stepPerRevolutionValue);
	m_AlpacaRestServer->use("/setup/rotationSpeed", &rotationSpeedValue);
	m_AlpacaRestServer->use("/setup/rotationAcceleration", &rotationAccelerationValue);
	m_AlpacaRestServer->put("/setup/restoreRotationMotorSettings", &restoreRotationMotorValues);
	m_AlpacaRestServer->use("/setup/unsafeDomeAction", &unsafeDomeAction);
	m_AlpacaRestServer->get("/setup/envCondition", &envConditionState);

#ifdef USE_WIFI
	m_AlpacaRestServer->use("/setup/shutterOpenOrder", &shutterOpenOrderValue);
	m_AlpacaRestServer->use("/setup/wifiSSID", &wifiSSIDValue);
	m_AlpacaRestServer->get("/setup/shutterPresentState", &isShutterPresentState);
	m_AlpacaRestServer->use("/setup/shutterSpeed", &shutterSpeedValue);
	m_AlpacaRestServer->use("/setup/shutterAcceleration", &shutterAccelerationValue);
	m_AlpacaRestServer->put("/setup/restoreShutterMotorSettings", &restoreShutterMotorValues);
	m_AlpacaRestServer->use("/setup/shutterWatchdogTimerValue", &shutterWatchdogTimerValue);
	m_AlpacaRestServer->use("/setup/shutterVoltageCutoff", &shutterVoltageCutoffValue);
	m_AlpacaRestServer->get("/setup/shutterVoltage", &shutterVoltageCutoffValue);
#endif

	// special endpoint to control the dome directly
	m_AlpacaRestServer->put("/setup/homeDome", &homeDome);
	m_AlpacaRestServer->put("/setup/parkDome", &parkDome);
	m_AlpacaRestServer->put("/setup/gotoAzimuth", &gotoAzimuth);
	m_AlpacaRestServer->get("/setup/getAzimuth", &getDomeAzimuth);
	m_AlpacaRestServer->put("/setup/openShutter", &openShutter);
	m_AlpacaRestServer->put("/setup/closeShutter", &closeShutter);
	m_AlpacaRestServer->get("/setup/getShutterState", &getShutterState);

	m_AlpacaRestServer->put("/setup/resetToFactory", &resetToFactory);

	m_AlpacaRestServer->put("/setup/abort", &uiAbort);

	DBPrintln("m_AlpacaRestServer started");
}


void DomeAlpacaServer::checkForRequest()
{
	// process incoming connections one at a time
	NetworkClient client = mRestServer->accept();
	if (client.connected()) {
		m_AlpacaRestServer->process(&client);
		client.stop();
		nTransactionID++;
  }
}
