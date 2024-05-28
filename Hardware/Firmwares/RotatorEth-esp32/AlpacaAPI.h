// Alpaca API prototype functions

#pragma message "Alpaca server enabled"
#include <atomic>
#include <vector>
#include <aWOT.h>
#include <functional>
#include <EthernetUdp.h>
#include <ArduinoJson.h>
// Alpaca REST server
#include <UUID.h>
#include <aWOT.h>

#define ALPACA_DISCOVERY_PORT 32227
#define ALPACA_SERVER_PORT 80
#define ALPACA_VAR_BUF_LEN 256
#define ALPACA_OK 0
#define DISCOVERY_ERROR -1

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


void formDataToJson(Request &req, JsonDocument &FormData)
{
	char name[ALPACA_VAR_BUF_LEN];
	char value[ALPACA_VAR_BUF_LEN];

	memset(name,0,ALPACA_VAR_BUF_LEN);
	memset(value,0,ALPACA_VAR_BUF_LEN);

	while(req.form(name, ALPACA_VAR_BUF_LEN-1, value, ALPACA_VAR_BUF_LEN-1)){
		DBPrintln("name : " + String(name));
		DBPrintln("value : " + String(value));
		if(isDigit(value[0]) ) {
			if(String(value).indexOf('.') == -1) {
				// int
				FormData[name]=String(value).toInt();
			} else {
				// double
				FormData[name]=String(value).toDouble();
			}
		}
		else {
			// string
			FormData[name]=String(value);
		}
	}
}


std::vector<std::vector<String>> getQueryGetVariables(String sQueryString)
{
	int nErr;
	int nIndex = 0;
	int nCurIndex = 0;
	String sEntry;
	std::vector<String> svKV;
	std::vector<String> svFields;
	std::vector<std::vector<String>> svParameters;
	
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
	return svParameters;
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
		svParameters = getQueryGetVariables(String(req.query()));
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
			if(FormData["ClientID"]) {
				serializeJson(FormData["ClientID"], sClientId);
				sClientId.trim();
				AlpacaResp["ClientID"] = sClientId.toInt()<0?0:sClientId.toInt();

			}
			if(FormData["ClientTransactionID"]) {
				serializeJson(FormData["ClientTransactionID"], sClientTransactionId);
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
	res.flush();
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
	res.flush();
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
	res.flush();
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
		res.sendStatus(400);
		AlpacaResp["ErrorNumber"] = 400;
		AlpacaResp["ErrorMessage"] = "Invalid parameters";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
		res.flush();
		return;
	}

	serializeJson(FormData["Action"], sAction);
	serializeJson(FormData["Parameters"], sParameters);
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
	res.flush();
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
		res.sendStatus(400);
		AlpacaResp["ErrorNumber"] = 400;
		AlpacaResp["ErrorMessage"] = "Invalid parameters";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
		res.flush();
		return;
	}

	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
	res.flush();
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
		res.sendStatus(400);
		AlpacaResp["ErrorNumber"] = 400;
		AlpacaResp["ErrorMessage"] = "Invalid parameters";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
		res.flush();
		return;
	}

	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	AlpacaResp["Value"] = true;
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
	res.flush();
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
		res.sendStatus(400);
		AlpacaResp["ErrorNumber"] = 400;
		AlpacaResp["ErrorMessage"] = "Invalid parameters";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
		res.flush();
		return;
	}

	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	AlpacaResp["Value"] = "Ok";
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
	res.flush();
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
	res.flush();
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

	DBPrintln("[ ********** setConected ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);
	res.set("Content-Type", "application/json");

	if(!bParamsOk){
		res.sendStatus(400);
		AlpacaResp["ErrorNumber"] = 400;
		AlpacaResp["ErrorMessage"] = "Invalid parameters";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
		res.flush();
		return;
	}

	if(!FormData["Connected"]) {
		res.sendStatus(400);
		AlpacaResp["ErrorNumber"] = 400;
		AlpacaResp["ErrorMessage"] = "Invalid parameters, missing 'Connected'";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
		res.flush();
		return;
	}
	serializeJson(FormData["Connected"], sParameter);
	sParameter.toLowerCase();

	if(!sParameter.equals("true")) {
		res.sendStatus(400);
		AlpacaResp["ErrorNumber"] = 400;
		AlpacaResp["ErrorMessage"] = "Invalid parameters";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
		res.flush();
		return;
	}

	bAlpacaConnected = sParameter.equals("true");
	DBPrintln("bAlpacaConnected : " + (bAlpacaConnected?String("true"):String("false")));

	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
	res.flush();
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
	res.flush();
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
	res.flush();
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
	res.flush();
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
	AlpacaResp["Value"]= 1;
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
	res.flush();
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
	res.flush();
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

	AlpacaResp["Value"].add("EthernetReconfigure");
	AlpacaResp["Value"].add("Calibrate");
	AlpacaResp["Value"].add("RestoreMotorDefault");
	AlpacaResp["Value"].add("GetRotatorAcceleration");
	AlpacaResp["Value"].add("SetRotatorAcceleration");
	AlpacaResp["Value"].add("GetMacAddress");
	AlpacaResp["Value"].add("GetIpAddress");
	AlpacaResp["Value"].add("SetIpAddress");
	AlpacaResp["Value"].add("RotatorVolts");
	AlpacaResp["Value"].add("GetRainAction");
	AlpacaResp["Value"].add("SetRainAction");
	AlpacaResp["Value"].add("isShutterPresent");
	AlpacaResp["Value"].add("GetSubnet");
	AlpacaResp["Value"].add("SetSubnet");
	AlpacaResp["Value"].add("GetPanID");
	AlpacaResp["Value"].add("SetPanID");
	AlpacaResp["Value"].add("GetRotatorSpeed");
	AlpacaResp["Value"].add("SetRotatorSpeed");
	AlpacaResp["Value"].add("GetStepPerRev");
	AlpacaResp["Value"].add("SetStepPerRev");
	AlpacaResp["Value"].add("GetIpGateway");
	AlpacaResp["Value"].add("SetIpGateway");
	AlpacaResp["Value"].add("GetDhcp");
	AlpacaResp["Value"].add("SetDhcp");
	AlpacaResp["Value"].add("GetRotatorReverse");
	AlpacaResp["Value"].add("SetRotatorReverse");
	AlpacaResp["Value"].add("GetRainStatus");
	AlpacaResp["Value"].add("RestoreMotorDefaultShutter");
	AlpacaResp["Value"].add("GetShutterAcceleration");
	AlpacaResp["Value"].add("SetShutterAcceleration");
	AlpacaResp["Value"].add("ShutterHello");
	AlpacaResp["Value"].add("GetShutterPanID");
	AlpacaResp["Value"].add("SetShutterPanID");
	AlpacaResp["Value"].add("GetShutterSpeed");
	AlpacaResp["Value"].add("SetShutterSpeed");
	AlpacaResp["Value"].add("GetShutterReverse");
	AlpacaResp["Value"].add("SetShutterReverse");

	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
	res.flush();
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
			AlpacaResp["Value"] = 90.0;
			break;
		case CLOSED:
			AlpacaResp["Value"] = 0.0;
			break;
		default:
			AlpacaResp["Value"] = 0.0;
			break;
	}
#else
	AlpacaResp["Value"] = 0.0;
#endif
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
	res.flush();

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
	res.flush();

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
	res.flush();

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
	res.flush();
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
	res.flush();
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
	res.flush();
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
	res.flush();
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
	res.flush();
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
	res.flush();
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

	AlpacaResp["Value"] = true;

	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
	res.flush();
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
	AlpacaResp["ErrorNumber"] = 1024;
	AlpacaResp["ErrorMessage"] = "Not implemented";

	AlpacaResp["Value"] = false;

	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
	res.flush();
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
	res.flush();
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
	AlpacaResp["ErrorNumber"] = 1024;
	AlpacaResp["ErrorMessage"] = "Not implemented";
#else
	if(!nbWiFiClient) {
		AlpacaResp["ErrorNumber"] = 1035;
		AlpacaResp["ErrorMessage"] = "Shutter not connected";
	} else {
		AlpacaResp["ErrorNumber"] = 0;
		AlpacaResp["ErrorMessage"] = "";
		shutterClient.print(sTmpString + "#");
		ReceiveWiFi(shutterClient);
		switch (RemoteShutter.state) {
			case OPEN:
				AlpacaResp["Value"] = A_OPEN;
				break;
			case CLOSED:
				AlpacaResp["Value"] = A_CLOSED;
				break;
			case ERROR:
				AlpacaResp["Value"] = A_ERROR;
				break;
			case OPENING:
			case BOTTOM_OPEN:
			case BOTTOM_OPENING:
			case FINISHING_OPEN:
				AlpacaResp["Value"] = A_OPENING;
				break;
			case CLOSING:
			case BOTTOM_CLOSED:
			case BOTTOM_CLOSING:
			case FINISHING_CLOSE:
				AlpacaResp["Value"] = A_CLOSING;
				break;
			default:
				AlpacaResp["Value"] = A_ERROR;
				break;
		}
	}
#endif
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
	res.flush();
}

void Slaved(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;

	DBPrintln("[ ********** Slaved ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);

	res.set("Content-Type", "application/json");

	if(!bParamsOk){
		res.sendStatus(400);
		AlpacaResp["ErrorNumber"] = 400;
		AlpacaResp["ErrorMessage"] = "Invalid parameters";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
		res.flush();
		return;
	}


	res.sendStatus(400);
	AlpacaResp["ErrorNumber"] = 400;
	AlpacaResp["ErrorNumber"] = 1024;
	AlpacaResp["ErrorMessage"] = "Can't slave";
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
	res.flush();
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
	res.flush();
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
		res.sendStatus(400);
		AlpacaResp["ErrorNumber"] = 400;
		AlpacaResp["ErrorMessage"] = "Invalid parameters";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
		res.flush();
		return;
	}

	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";

	Abort(); // this is in the RotatorEth-esp32.ino

	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
	res.flush();
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
		res.sendStatus(400);
		AlpacaResp["ErrorNumber"] = 400;
		AlpacaResp["ErrorMessage"] = "Invalid parameters";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
		res.flush();
		return;
	}

#ifndef USE_WIFI
	AlpacaResp["ErrorNumber"] = 1024;
	AlpacaResp["ErrorMessage"] = "Not implemented";
#else
	if(!nbWiFiClient) {
		AlpacaResp["ErrorNumber"] = 1035;
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
	res.flush();
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
		res.sendStatus(400);
		AlpacaResp["ErrorNumber"] = 400;
		AlpacaResp["ErrorMessage"] = "Invalid parameters";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
		res.flush();
		return;
	}

	if(bLowShutterVoltage) {
		AlpacaResp["ErrorNumber"] = 1032;
		AlpacaResp["ErrorMessage"] = "Low shutter voltage, staying at park position";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
		res.flush();
		return;
	}

	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";

	Rotator->StartHoming();

	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
	res.flush();
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
		res.sendStatus(400);
		AlpacaResp["ErrorNumber"] = 400;
		AlpacaResp["ErrorMessage"] = "Invalid parameters";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
		res.flush();
		return;
	}

#ifndef USE_WIFI
	AlpacaResp["ErrorNumber"] = 1024;
	AlpacaResp["ErrorMessage"] = "Not implemented";
#else
	if(!nbWiFiClient) {
		AlpacaResp["ErrorNumber"] = 1035;
		AlpacaResp["ErrorMessage"] = "Shutter not connected";
	}
	else if(bLowShutterVoltage) {
		AlpacaResp["ErrorNumber"] = 1032;
		AlpacaResp["ErrorMessage"] = "Low shutter voltage, staying at park position";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
		res.flush();
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
	res.flush();
}

void doPark(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;
	double fParkPos;

	DBPrintln("[ ********** doPark ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);

	res.set("Content-Type", "application/json");

	if(!bParamsOk){
		res.sendStatus(400);
		AlpacaResp["ErrorNumber"] = 400;
		AlpacaResp["ErrorMessage"] = "Invalid parameters";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
		res.flush();
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
	res.flush();
}

void setPark(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;
	double fParkPos;

	DBPrintln("[ ********** setPark ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);

	res.set("Content-Type", "application/json");

	if(!bParamsOk){
		res.sendStatus(400);
		AlpacaResp["ErrorNumber"] = 400;
		AlpacaResp["ErrorMessage"] = "Invalid parameters";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
		res.flush();
		return;
	}

	fParkPos = Rotator->GetAzimuth();
	Rotator->SetParkAzimuth(fParkPos);

	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
	res.flush();
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
		res.sendStatus(400);
		AlpacaResp["ErrorNumber"] = 400;
		AlpacaResp["ErrorMessage"] = "Invalid parameters";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
		res.flush();
		return;
	}

	if(!FormData["Altitude"]) {
		res.sendStatus(400);
		AlpacaResp["ErrorNumber"] = 400;
		AlpacaResp["ErrorMessage"] = "Invalid value";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
		res.flush();
		return;
	}
	// in case we implement this one day.
	AlpacaResp["ErrorNumber"] = 1024;
	AlpacaResp["ErrorMessage"] = "Not implemented";
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);
#else
	AlpacaResp["ErrorNumber"] = 1024;
	AlpacaResp["ErrorMessage"] = "Invalid method";
	serializeJson(AlpacaResp, sResp);
#endif

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
	res.flush();

}

void doGoTo(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;
	double dNewPos;

	DBPrintln("[ ********** doGoTo ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);

	res.set("Content-Type", "application/json");

	if(!bParamsOk){
		res.sendStatus(400);
		AlpacaResp["ErrorNumber"] = 400;
		AlpacaResp["ErrorMessage"] = "Invalid parameters";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
		res.flush();
		return;
	}

	if(bLowShutterVoltage) {
		AlpacaResp["ErrorNumber"] = 1032;
		AlpacaResp["ErrorMessage"] = "Low shutter voltage, staying at park position";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
		res.flush();
		return;
	}

	if(!FormData["Azimuth"]) {
		res.sendStatus(400);
		AlpacaResp["ErrorNumber"] = 400;
		AlpacaResp["ErrorMessage"] = "Invalid parameters";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
		res.flush();
		return;
	}

	dNewPos = FormData["Azimuth"];
	if(dNewPos < 0 || dNewPos>360) {
		AlpacaResp["ErrorNumber"] = 1025;
		AlpacaResp["ErrorMessage"] = "Invalid azimuth";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
		res.flush();
		return;
	}

	Rotator->GoToAzimuth(dNewPos);

	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
	res.flush();
}

void doSyncAzimuth(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	bool bParamsOk = false;
	String sResp;
	double dNewPos;

	DBPrintln("[ ********** doSyncAzimuth ********** ]");
	bParamsOk = getIDs(req, AlpacaResp, FormData);

	res.set("Content-Type", "application/json");

	if(!bParamsOk){
		res.sendStatus(400);
		AlpacaResp["ErrorNumber"] = 400;
		AlpacaResp["ErrorMessage"] = "Invalid parameters";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
		res.flush();
		return;
	}

	if(!FormData["Azimuth"]) {
		res.sendStatus(400);
		AlpacaResp["ErrorNumber"] = 1025;
		AlpacaResp["ErrorMessage"] = "Invalid azimuth";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
		res.flush();
		return;
	}

	dNewPos = FormData["Azimuth"];
	if(dNewPos<0 || dNewPos > 360) {
		AlpacaResp["ErrorNumber"] = 1025;
		AlpacaResp["ErrorMessage"] = "Invalid Azimuth";
		serializeJson(AlpacaResp, sResp);
		res.write((uint8_t*)(sResp.c_str()),sResp.length());
		res.flush();
		return;
	}

	Rotator->SyncPosition(dNewPos);

	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.write((uint8_t*)(sResp.c_str()),sResp.length());
	res.flush();
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
	res.flush();
}


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

	uuid.generate();

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

	m_AlpacaRestServer->use("/api/v1/dome/0/slaved", &Slaved);

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
