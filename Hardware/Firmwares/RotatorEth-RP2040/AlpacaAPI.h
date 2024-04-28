// Alpaca API prototype functions
#include <aWOT.h>

#pragma message "Alpaca server enabled"

#define ALPACA_VAR_BUF_LEN 256
#define ALPACA_OK 0

String sAlpacaDiscovery = "alpacadiscovery1";
#include <functional>
#include <EthernetUdp.h>
#include <ArduinoJson.h>
// Alpaca REST server
#include <UUID.h>
#include <aWOT.h>
uint32_t nTransactionID;
UUID uuid;
#define SERVER_ERROR -1
#define ALPACA_DISCOVERY_PORT 32227
#define ALPACA_SERVER_PORT 80
String sRedirectURL;

enum ShutterStates { OPEN, CLOSED, OPENING, CLOSING, BOTTOM_OPEN, BOTTOM_CLOSED, BOTTOM_OPENING, BOTTOM_CLOSING, ERROR, FINISHING_OPEN, FINISHING_CLOSE };
enum AlpacaShutterStates { A_OPEN=0, A_CLOSED, A_OPENING, A_CLOSING,  A_ERROR};

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
	int nBeginOk = 0;
	discoveryServer = new EthernetUDP();
	if(!discoveryServer) {
		discoveryServer = nullptr;
		return;
	}
	DBPrintln("Binding Alpaca UDP discovery server to " + IpAddress2String(Ethernet.localIP()));
	nBeginOk =  discoveryServer->begin(m_UDPPort);
	if(!nBeginOk) {
		DBPrintln("Error binding UDP Alapca server to  " + IpAddress2String(Ethernet.localIP()) + " on port " + String(m_UDPPort));
		return;
	}

	DBPrintln("Alpaca discovery server started on port " + String(m_UDPPort));
}

int DomeAlpacaDiscoveryServer::checkForRequest()
{
	if(!discoveryServer)
		return -1;
	
	String sDiscoveryResponse = "{\"AlpacaPort\":"+String(ALPACA_SERVER_PORT)+"}";
	String sDiscoveryRequest;

	char packetBuffer[UDP_TX_PACKET_MAX_SIZE+1];
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
			return SERVER_ERROR; // wrong type of discovery message
		}
		DBPrintln("Alpaca discovery server sending response : " + sDiscoveryResponse);
		// send discovery reponse
		discoveryServer->beginPacket(discoveryServer->remoteIP(), discoveryServer->remotePort());
		discoveryServer->write(sDiscoveryResponse.c_str());
		discoveryServer->endPacket();
	}
	return ALPACA_OK;
}



JsonDocument formDataToJson(Request &req)
{
	JsonDocument FormData;
	char name[ALPACA_VAR_BUF_LEN];
	char value[ALPACA_VAR_BUF_LEN];

	while(req.form(name, ALPACA_VAR_BUF_LEN, value, ALPACA_VAR_BUF_LEN)){
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
	return FormData;
}

void redirectToSetup(Request &req, Response &res)
{
	res.set("Location", sRedirectURL.c_str());
    res.sendStatus(302);
}

void getApiVersion(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	String sResp;
	char ClientID[64];
	char ClientTransactionID[64];
	
	DBPrintln("getApiVersion");
	req.query("ClientID", ClientID, 64);
	req.query("ClientTransactionID", ClientTransactionID, 64);
	DBPrintln("ClientID : " + String(ClientID));
	DBPrintln("ClientTransactionID : " + String(ClientTransactionID));

	res.set("Content-Type", "application/json");

	AlpacaResp["Value"][0] = 1;

	if(strlen(ClientID))
		AlpacaResp["ClientID"] = uint32_t(atoi(ClientID)<0?0:atoi(ClientID));
	if(strlen(ClientTransactionID))
		AlpacaResp["ClientTransactionID"] = uint32_t(atoi(ClientTransactionID)<0?0:atoi(ClientTransactionID));
	AlpacaResp["ServerTransactionID"] = nTransactionID;

	serializeJson(AlpacaResp, sResp);

	DBPrintln("sResp : " + sResp);

	res.print(sResp);
	res.flush();
}

void getDescription(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	String sResp;
	char ClientID[64];
	char ClientTransactionID[64];
	
	DBPrintln("[getDescription]");
	req.query("ClientID", ClientID, 64);
	req.query("ClientTransactionID", ClientTransactionID, 64);
	DBPrintln("ClientID : " + String(ClientID));
	DBPrintln("ClientTransactionID : " + String(ClientTransactionID));

	res.set("Content-Type", "application/json");
	AlpacaResp["Value"]["ServerName"]= "RTIDome Alpaca";
	AlpacaResp["Value"]["Manufacturer"]= "RTI-Zone";
	AlpacaResp["Value"]["ManufacturerVersion"]= VERSION;
	AlpacaResp["Value"]["Location"]= "Earth";

	if(strlen(ClientID))
		AlpacaResp["ClientID"] = uint32_t(atoi(ClientID)<0?0:atoi(ClientID));
	if(strlen(ClientTransactionID))
		AlpacaResp["ClientTransactionID"] = uint32_t(atoi(ClientTransactionID)<0?0:atoi(ClientTransactionID));
	AlpacaResp["ServerTransactionID"] = nTransactionID;

	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.print(sResp);
	res.flush();
}

void getConfiguredDevice(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	String sResp;
	char ClientID[64];
	char ClientTransactionID[64];
	
	DBPrintln("[getConfiguredDevice]");
	req.query("ClientID", ClientID, 64);
	req.query("ClientTransactionID", ClientTransactionID, 64);
	DBPrintln("ClientID : " + String(ClientID));
	DBPrintln("ClientTransactionID : " + String(ClientTransactionID));

	res.set("Content-Type", "application/json");
	AlpacaResp["Value"][0] ["DeviceName"]= "RTIDome";
	AlpacaResp["Value"][0] ["DeviceType"]= "dome";
	AlpacaResp["Value"][0] ["DeviceNumber"]= 0;
	AlpacaResp["Value"][0] ["UniqueID"]= uuid;
	if(strlen(ClientID))
		AlpacaResp["ClientID"] = uint32_t(atoi(ClientID)<0?0:atoi(ClientID));
	if(strlen(ClientTransactionID))
		AlpacaResp["ClientTransactionID"] = uint32_t(atoi(ClientTransactionID)<0?0:atoi(ClientTransactionID));
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.print(sResp);
	res.flush();
}

void doAction(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	String sResp;
	String sAction;
	String sParameters;

	DBPrintln("[doAction]");
	FormData = formDataToJson(req);

#ifdef DEBUG
	serializeJson(FormData, sResp);
	DBPrintln("FormData : " + sResp);
	DBPrintln("FormData.size() : " + String(FormData.size()));
	sResp="";
#endif

	if(FormData.size()==0){
		res.set("Content-Type", "application/json");
		res.sendStatus(400);
		AlpacaResp["ErrorNumber"] = 400;
		AlpacaResp["ErrorMessage"] = "Invalid parameters";
		AlpacaResp["ServerTransactionID"] = nTransactionID;
		serializeJson(AlpacaResp, sResp);
		res.print(sResp);
		res.flush();
		return;
	}

	sAction = String(FormData["Action"]);
	sParameters = String(FormData["Parameters"]);
#ifdef DEBUG
	DBPrintln("sAction : " + sAction);
	DBPrintln("sParameters : " + sParameters);
#endif

	res.set("Content-Type", "application/json");
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	AlpacaResp["Value"] = "Ok";
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	if(FormData["ClientID"])
		AlpacaResp["ClientID"] = uint32_t(FormData["ClientID"]);
	if(FormData["ClientTransactionID"])
		AlpacaResp["ClientTransactionID"] = uint32_t(FormData["ClientTransactionID"]);
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.print(sResp);
	res.flush();
}

void doCommandBlind(Request &req, Response &res)
{
	 JsonDocument AlpacaResp;
	JsonDocument FormData;
	String sResp;

	DBPrintln("[doCommandBlind]");
	FormData = formDataToJson(req);

#ifdef DEBUG
	serializeJson(FormData, sResp);
	DBPrintln("FormData : " + sResp);
	DBPrintln("FormData.size() : " + String(FormData.size()));
	sResp="";
#endif

	if(FormData.size()==0){
		res.set("Content-Type", "application/json");
		res.sendStatus(400);
		AlpacaResp["ErrorNumber"] = 400;
		AlpacaResp["ErrorMessage"] = "Invalid parameters";
		AlpacaResp["ServerTransactionID"] = nTransactionID;
		serializeJson(AlpacaResp, sResp);
		res.print(sResp);
		res.flush();
		return;
	}

	res.set("Content-Type", "application/json");
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	if(FormData["ClientID"])
		AlpacaResp["ClientID"] = uint32_t(FormData["ClientID"]);
	if(FormData["ClientTransactionID"])
		AlpacaResp["ClientTransactionID"] = uint32_t(FormData["ClientTransactionID"]);
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.print(sResp);
	res.flush();
}

void doCommandBool(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	String sResp;

	DBPrintln("[doCommandBool]");
	FormData = formDataToJson(req);
#ifdef DEBUG
	serializeJson(FormData, sResp);
	DBPrintln("FormData : " + sResp);
	DBPrintln("FormData.size() : " + String(FormData.size()));
	sResp="";
#endif

	if(FormData.size()==0){
		res.set("Content-Type", "application/json");
		res.sendStatus(400);
		AlpacaResp["ErrorNumber"] = 400;
		AlpacaResp["ErrorMessage"] = "Invalid parameters";
		AlpacaResp["ServerTransactionID"] = nTransactionID;
		serializeJson(AlpacaResp, sResp);
		res.print(sResp);
		res.flush();
		return;
	}

	res.set("Content-Type", "application/json");
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	AlpacaResp["Value"] = true;
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	if(FormData["ClientID"])
		AlpacaResp["ClientID"] = uint32_t(FormData["ClientID"]);
	if(FormData["ClientTransactionID"])
		AlpacaResp["ClientTransactionID"] = uint32_t(FormData["ClientTransactionID"]);
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.print(sResp);
	res.flush();
}

void doCommandString(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	String sResp;

	DBPrintln("[doCommandString]");
	FormData = formDataToJson(req);
#ifdef DEBUG
	serializeJson(FormData, sResp);
	DBPrintln("FormData : " + sResp);
	DBPrintln("FormData.size() : " + String(FormData.size()));
	sResp="";
#endif

	if(FormData.size()==0){
		res.set("Content-Type", "application/json");
		res.sendStatus(400);
		AlpacaResp["ErrorNumber"] = 400;
		AlpacaResp["ErrorMessage"] = "Invalid parameters";
		AlpacaResp["ServerTransactionID"] = nTransactionID;
		serializeJson(AlpacaResp, sResp);
		res.print(sResp);
		res.flush();
		return;
	}

	res.set("Content-Type", "application/json");
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	AlpacaResp["Value"] = "Ok";
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	if(FormData["ClientID"])
		AlpacaResp["ClientID"] = uint32_t(FormData["ClientID"]);
	if(FormData["ClientTransactionID"])
		AlpacaResp["ClientTransactionID"] = uint32_t(FormData["ClientTransactionID"]);
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.print(sResp);
	res.flush();
}

void getConnected(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	String sResp;
	char ClientID[64];
	char ClientTransactionID[64];
	
	DBPrintln("[getConected]");
	req.query("ClientID", ClientID, 64);
	req.query("ClientTransactionID", ClientTransactionID, 64);
	DBPrintln("ClientID : " + String(ClientID));
	DBPrintln("ClientTransactionID : " + String(ClientTransactionID));

	res.set("Content-Type", "application/json");
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	if(strlen(ClientID))
		AlpacaResp["ClientID"] = uint32_t(atoi(ClientID)<0?0:atoi(ClientID));
	if(strlen(ClientTransactionID))
		AlpacaResp["ClientTransactionID"] = uint32_t(atoi(ClientTransactionID)<0?0:atoi(ClientTransactionID));
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	AlpacaResp["Value"] = bAlpacaConnected;
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.print(sResp);
	res.flush();
}

void setConnected(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	String sResp;

	DBPrintln("[setConected]");
	FormData = formDataToJson(req);
#ifdef DEBUG
	serializeJson(FormData, sResp);
	DBPrintln("FormData : " + sResp);
	DBPrintln("FormData.size() : " + String(FormData.size()));
	sResp="";
#endif

	if(FormData.size()==0){
		res.set("Content-Type", "application/json");
		res.sendStatus(400);
		AlpacaResp["ErrorNumber"] = 400;
		AlpacaResp["ErrorMessage"] = "Invalid parameters";
		AlpacaResp["ServerTransactionID"] = nTransactionID;
		serializeJson(AlpacaResp, sResp);
		res.print(sResp);
		res.flush();
		return;
	}

	if(!FormData["Connected"]) {
		res.set("Content-Type", "application/json");
		res.sendStatus(400);
		AlpacaResp["ErrorNumber"] = 400;
		AlpacaResp["ErrorMessage"] = "Invalid parameters";
		AlpacaResp["ServerTransactionID"] = nTransactionID;
		if(FormData["ClientID"])
			AlpacaResp["ClientID"] = uint32_t(FormData["ClientID"]);
		if(FormData["ClientTransactionID"])
			AlpacaResp["ClientTransactionID"] = uint32_t(FormData["ClientTransactionID"]);
		serializeJson(AlpacaResp, sResp);
		res.print(sResp);
		res.flush();
		return;
	}

	bAlpacaConnected = (FormData["Connected"] == String("True"));
	DBPrintln("bAlpacaConnected : " + (bAlpacaConnected?String("True"):String("False")));

	res.set("Content-Type", "application/json");
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	if(FormData["ClientID"])
		AlpacaResp["ClientID"] = uint32_t(FormData["ClientID"]);
	if(FormData["ClientTransactionID"]) 
		AlpacaResp["ClientTransactionID"] = uint32_t(FormData["ClientTransactionID"]);
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.print(sResp);
	res.flush();
}

void getDeviceDescription(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	String sResp;
	char ClientID[64];
	char ClientTransactionID[64];
	
	DBPrintln("[getDeviceDescription]");
	req.query("ClientID", ClientID, 64);
	req.query("ClientTransactionID", ClientTransactionID, 64);
	DBPrintln("ClientID : " + String(ClientID));
	DBPrintln("ClientTransactionID : " + String(ClientTransactionID));

	res.set("Content-Type", "application/json");
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	if(strlen(ClientTransactionID))
		AlpacaResp["ClientTransactionID"] = uint32_t(abs(atoi(ClientTransactionID)));
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	AlpacaResp["Value"]= "RTI-Zone dome controller";
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.print(sResp);
	res.flush();
}

void getDriverInfo(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	String sResp;
	char ClientID[64];
	char ClientTransactionID[64];
	
	DBPrintln("[getDriverInfo]");
	req.query("ClientID", ClientID, 64);
	req.query("ClientTransactionID", ClientTransactionID, 64);
	DBPrintln("ClientID : " + String(ClientID));
	DBPrintln("ClientTransactionID : " + String(ClientTransactionID));

	res.set("Content-Type", "application/json");
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	if(strlen(ClientID))
		AlpacaResp["ClientID"] = uint32_t(atoi(ClientID)<0?0:atoi(ClientID));
	if(strlen(ClientTransactionID))
		AlpacaResp["ClientTransactionID"] = uint32_t(atoi(ClientTransactionID)<0?0:atoi(ClientTransactionID));
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	AlpacaResp["Value"]= "RTI-Zone Dome controller";
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.print(sResp);
	res.flush();
}

void getDriverVersion(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	String sResp;
	char ClientID[64];
	char ClientTransactionID[64];
	
	DBPrintln("[getDriverVersion]");
	req.query("ClientID", ClientID, 64);
	req.query("ClientTransactionID", ClientTransactionID, 64);
	DBPrintln("ClientID : " + String(ClientID));
	DBPrintln("ClientTransactionID : " + String(ClientTransactionID));

	res.set("Content-Type", "application/json");
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	if(strlen(ClientID))
		AlpacaResp["ClientID"] = uint32_t(atoi(ClientID)<0?0:atoi(ClientID));
	if(strlen(ClientTransactionID))
		AlpacaResp["ClientTransactionID"] = uint32_t(atoi(ClientTransactionID)<0?0:atoi(ClientTransactionID));
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	AlpacaResp["Value"]= String(VERSION);
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.print(sResp);
	res.flush();
}

void getInterfaceVersion(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	String sResp;
	char ClientID[64];
	char ClientTransactionID[64];
	
	DBPrintln("[getInterfaceVersion]");
	req.query("ClientID", ClientID, 64);
	req.query("ClientTransactionID", ClientTransactionID, 64);
	DBPrintln("ClientID : " + String(ClientID));
	DBPrintln("ClientTransactionID : " + String(ClientTransactionID));

	res.set("Content-Type", "application/json");
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	if(strlen(ClientID))
		AlpacaResp["ClientID"] = uint32_t(atoi(ClientID)<0?0:atoi(ClientID));
	if(strlen(ClientTransactionID))
		AlpacaResp["ClientTransactionID"] = uint32_t(atoi(ClientTransactionID)<0?0:atoi(ClientTransactionID));
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	AlpacaResp["Value"]= 1;
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.print(sResp);
	res.flush();
}

void getName(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	String sResp;
	char ClientID[64];
	char ClientTransactionID[64];
	
	DBPrintln("[getName]");
	req.query("ClientID", ClientID, 64);
	req.query("ClientTransactionID", ClientTransactionID, 64);
	DBPrintln("ClientID : " + String(ClientID));
	DBPrintln("ClientTransactionID : " + String(ClientTransactionID));

	res.set("Content-Type", "application/json");
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	if(strlen(ClientID))
		AlpacaResp["ClientID"] = uint32_t(atoi(ClientID)<0?0:atoi(ClientID));
	if(strlen(ClientTransactionID))
		AlpacaResp["ClientTransactionID"] = uint32_t(atoi(ClientTransactionID)<0?0:atoi(ClientTransactionID));
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	AlpacaResp["Value"]= "RTI-Zone Dome controller";
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.print(sResp);
	res.flush(); 
}

void getSupportedActions(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	String sResp;
	char ClientID[64];
	char ClientTransactionID[64];
	
	DBPrintln("[getSupportedActions]");
	req.query("ClientID", ClientID, 64);
	req.query("ClientTransactionID", ClientTransactionID, 64);
	DBPrintln("ClientID : " + String(ClientID));
	DBPrintln("ClientTransactionID : " + String(ClientTransactionID));

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

	AlpacaResp["ServerTransactionID"] = nTransactionID;
	if(strlen(ClientID))
		AlpacaResp["ClientID"] = uint32_t(atoi(ClientID)<0?0:atoi(ClientID));
	if(strlen(ClientTransactionID))
		AlpacaResp["ClientTransactionID"] = uint32_t(atoi(ClientTransactionID)<0?0:atoi(ClientTransactionID));
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.print(sResp);
	res.flush();
}

void getAltitude(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	String sResp;
	char ClientID[64];
	char ClientTransactionID[64];
	String sTmpString = String(STATE_SHUTTER);

	DBPrintln("[getAltitude]");
	req.query("ClientID", ClientID, 64);
	req.query("ClientTransactionID", ClientTransactionID, 64);
	DBPrintln("ClientID : " + String(ClientID));
	DBPrintln("ClientTransactionID : " + String(ClientTransactionID));

	res.set("Content-Type", "application/json");
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	if(strlen(ClientID))
		AlpacaResp["ClientID"] = uint32_t(atoi(ClientID)<0?0:atoi(ClientID));
	if(strlen(ClientTransactionID))
		AlpacaResp["ClientTransactionID"] = uint32_t(atoi(ClientTransactionID)<0?0:atoi(ClientTransactionID));
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

	res.print(sResp);
	res.flush(); 

}

void geAtHome(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	String sResp;
	char ClientID[64];
	char ClientTransactionID[64];
	
	DBPrintln("[geAtHome]");
	req.query("ClientID", ClientID, 64);
	req.query("ClientTransactionID", ClientTransactionID, 64);
	DBPrintln("ClientID : " + String(ClientID));
	DBPrintln("ClientTransactionID : " + String(ClientTransactionID));

	res.set("Content-Type", "application/json");
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	if(strlen(ClientID))
		AlpacaResp["ClientID"] = uint32_t(atoi(ClientID)<0?0:atoi(ClientID));
	if(strlen(ClientTransactionID))
		AlpacaResp["ClientTransactionID"] = uint32_t(atoi(ClientTransactionID)<0?0:atoi(ClientTransactionID));
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

	res.print(sResp);
	res.flush(); 

}

void geAtPark(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	String sResp;
	char ClientID[64];
	char ClientTransactionID[64];
	
	DBPrintln("[geAtPark]");
	req.query("ClientID", ClientID, 64);
	req.query("ClientTransactionID", ClientTransactionID, 64);
	DBPrintln("ClientID : " + String(ClientID));
	DBPrintln("ClientTransactionID : " + String(ClientTransactionID));

	res.set("Content-Type", "application/json");
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	if(strlen(ClientTransactionID))
		AlpacaResp["ClientTransactionID"] = uint32_t(abs(atoi(ClientTransactionID)));
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";

	if(bParked) {
		AlpacaResp["Value"] = true;
	}
	else {
		AlpacaResp["Value"] = false;
	}
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.print(sResp);
	res.flush(); 

}

void getAzimuth(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	String sResp;
	char ClientID[64];
	char ClientTransactionID[64];
	
	DBPrintln("[getAzimuth]");
	req.query("ClientID", ClientID, 64);
	req.query("ClientTransactionID", ClientTransactionID, 64);
	DBPrintln("ClientID : " + String(ClientID));
	DBPrintln("ClientTransactionID : " + String(ClientTransactionID));

	res.set("Content-Type", "application/json");
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	if(strlen(ClientID))
		AlpacaResp["ClientID"] = uint32_t(atoi(ClientID)<0?0:atoi(ClientID));
	if(strlen(ClientTransactionID))
		AlpacaResp["ClientTransactionID"] = uint32_t(atoi(ClientTransactionID)<0?0:atoi(ClientTransactionID));
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	
	AlpacaResp["Value"] = Rotator->GetAzimuth();

	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.print(sResp);
	res.flush(); 
} 

void canfindhome(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	String sResp;
	char ClientID[64];
	char ClientTransactionID[64];
	
	DBPrintln("canfindhome");
	req.query("ClientID", ClientID, 64);
	req.query("ClientTransactionID", ClientTransactionID, 64);
	DBPrintln("ClientID : " + String(ClientID));
	DBPrintln("ClientTransactionID : " + String(ClientTransactionID));

	res.set("Content-Type", "application/json");
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	if(strlen(ClientID))
		AlpacaResp["ClientID"] = uint32_t(atoi(ClientID)<0?0:atoi(ClientID));
	if(strlen(ClientTransactionID))
		AlpacaResp["ClientTransactionID"] = uint32_t(atoi(ClientTransactionID)<0?0:atoi(ClientTransactionID));
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	
	AlpacaResp["Value"] = true;

	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.print(sResp);
	res.flush(); 
}

void canPark(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	String sResp;
	char ClientID[64];
	char ClientTransactionID[64];
	
	DBPrintln("canPark");
	req.query("ClientID", ClientID, 64);
	req.query("ClientTransactionID", ClientTransactionID, 64);
	DBPrintln("ClientID : " + String(ClientID));
	DBPrintln("ClientTransactionID : " + String(ClientTransactionID));

	res.set("Content-Type", "application/json");
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	if(strlen(ClientID))
		AlpacaResp["ClientID"] = uint32_t(atoi(ClientID)<0?0:atoi(ClientID));
	if(strlen(ClientTransactionID))
		AlpacaResp["ClientTransactionID"] = uint32_t(atoi(ClientTransactionID)<0?0:atoi(ClientTransactionID));
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	
	AlpacaResp["Value"] = true;

	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.print(sResp);
	res.flush(); 
}

void canSetAltitude(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	String sResp;
	char ClientID[64];
	char ClientTransactionID[64];
	
	DBPrintln("canSetAltitude");
	req.query("ClientID", ClientID, 64);
	req.query("ClientTransactionID", ClientTransactionID, 64);
	DBPrintln("ClientID : " + String(ClientID));
	DBPrintln("ClientTransactionID : " + String(ClientTransactionID));

	res.set("Content-Type", "application/json");
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	if(strlen(ClientID))
		AlpacaResp["ClientID"] = uint32_t(atoi(ClientID)<0?0:atoi(ClientID));
	if(strlen(ClientTransactionID))
		AlpacaResp["ClientTransactionID"] = uint32_t(atoi(ClientTransactionID)<0?0:atoi(ClientTransactionID));
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	
	AlpacaResp["Value"] = false;

	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.print(sResp);
	res.flush(); 
}

void canSetAzimuth(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	String sResp;
	char ClientID[64];
	char ClientTransactionID[64];
	
	DBPrintln("canSetAzimuth");
	req.query("ClientID", ClientID, 64);
	req.query("ClientTransactionID", ClientTransactionID, 64);
	DBPrintln("ClientID : " + String(ClientID));
	DBPrintln("ClientTransactionID : " + String(ClientTransactionID));

	res.set("Content-Type", "application/json");
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	if(strlen(ClientID))
		AlpacaResp["ClientID"] = uint32_t(atoi(ClientID)<0?0:atoi(ClientID));
	if(strlen(ClientTransactionID))
		AlpacaResp["ClientTransactionID"] = uint32_t(atoi(ClientTransactionID)<0?0:atoi(ClientTransactionID));
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	
	AlpacaResp["Value"] = true;

	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.print(sResp);
	res.flush(); 
}

void canSetPark(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	String sResp;
	char ClientID[64];
	char ClientTransactionID[64];
	
	DBPrintln("canSetPark");
	req.query("ClientID", ClientID, 64);
	req.query("ClientTransactionID", ClientTransactionID, 64);
	DBPrintln("ClientID : " + String(ClientID));
	DBPrintln("ClientTransactionID : " + String(ClientTransactionID));

	res.set("Content-Type", "application/json");
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	if(strlen(ClientID))
		AlpacaResp["ClientID"] = uint32_t(atoi(ClientID)<0?0:atoi(ClientID));
	if(strlen(ClientTransactionID))
		AlpacaResp["ClientTransactionID"] = uint32_t(atoi(ClientTransactionID)<0?0:atoi(ClientTransactionID));
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	
	AlpacaResp["Value"] = true;

	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.print(sResp);
	res.flush(); 
}

void canSetShutter(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	String sResp;
	char ClientID[64];
	char ClientTransactionID[64];
	
	DBPrintln("canSetShutter");
	req.query("ClientID", ClientID, 64);
	req.query("ClientTransactionID", ClientTransactionID, 64);
	DBPrintln("ClientID : " + String(ClientID));
	DBPrintln("ClientTransactionID : " + String(ClientTransactionID));

	res.set("Content-Type", "application/json");
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	if(strlen(ClientID))
		AlpacaResp["ClientID"] = uint32_t(atoi(ClientID)<0?0:atoi(ClientID));
	if(strlen(ClientTransactionID))
		AlpacaResp["ClientTransactionID"] = uint32_t(atoi(ClientTransactionID)<0?0:atoi(ClientTransactionID));
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	
	AlpacaResp["Value"] = true;

	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.print(sResp);
	res.flush(); 
}

void canSlave(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	String sResp;
	char ClientID[64];
	char ClientTransactionID[64];
	
	DBPrintln("canSlave");
	req.query("ClientID", ClientID, 64);
	req.query("ClientTransactionID", ClientTransactionID, 64);
	DBPrintln("ClientID : " + String(ClientID));
	DBPrintln("ClientTransactionID : " + String(ClientTransactionID));

	res.set("Content-Type", "application/json");
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	if(strlen(ClientID))
		AlpacaResp["ClientID"] = uint32_t(atoi(ClientID)<0?0:atoi(ClientID));
	if(strlen(ClientTransactionID))
		AlpacaResp["ClientTransactionID"] = uint32_t(atoi(ClientTransactionID)<0?0:atoi(ClientTransactionID));
	AlpacaResp["ErrorNumber"] = 1024;
	AlpacaResp["ErrorMessage"] = "Not implemented";
	
	AlpacaResp["Value"] = false;

	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.print(sResp);
	res.flush(); 
}

void canSyncAzimuth(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	String sResp;
	char ClientID[64];
	char ClientTransactionID[64];
	
	DBPrintln("canSyncAzimuth");
	req.query("ClientID", ClientID, 64);
	req.query("ClientTransactionID", ClientTransactionID, 64);
	DBPrintln("ClientID : " + String(ClientID));
	DBPrintln("ClientTransactionID : " + String(ClientTransactionID));

	res.set("Content-Type", "application/json");
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	if(strlen(ClientID))
		AlpacaResp["ClientID"] = uint32_t(atoi(ClientID)<0?0:atoi(ClientID));
	if(strlen(ClientTransactionID))
		AlpacaResp["ClientTransactionID"] = uint32_t(atoi(ClientTransactionID)<0?0:atoi(ClientTransactionID));
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	
	AlpacaResp["Value"] = true;

	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.print(sResp);
	res.flush(); 
}

void getShutterStatus(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	String sResp;
	char ClientID[64];
	char ClientTransactionID[64];
	String sTmpString = String(STATE_SHUTTER);

	DBPrintln("getShutterStatus");
	req.query("ClientID", ClientID, 64);
	req.query("ClientTransactionID", ClientTransactionID, 64);
	DBPrintln("ClientID : " + String(ClientID));
	DBPrintln("ClientTransactionID : " + String(ClientTransactionID));
#ifndef USE_WIFI
	res.set("Content-Type", "application/json");
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	if(strlen(ClientID))
		AlpacaResp["ClientID"] = uint32_t(atoi(ClientID)<0?0:atoi(ClientID));
	if(strlen(ClientTransactionID))
		AlpacaResp["ClientTransactionID"] = uint32_t(atoi(ClientTransactionID)<0?0:atoi(ClientTransactionID));
	AlpacaResp["ErrorNumber"] = 1024;
	AlpacaResp["ErrorMessage"] = "Not implemented";
#else
	if(!nbWiFiClient) {
		res.set("Content-Type", "application/json");
		AlpacaResp["ServerTransactionID"] = nTransactionID;
		if(strlen(ClientID))
			AlpacaResp["ClientID"] = uint32_t(atoi(ClientID)<0?0:atoi(ClientID));
		if(strlen(ClientTransactionID))
			AlpacaResp["ClientTransactionID"] = uint32_t(atoi(ClientTransactionID)<0?0:atoi(ClientTransactionID));
		AlpacaResp["ErrorNumber"] = 1035;
		AlpacaResp["ErrorMessage"] = "Shutter not connected";
	} else {
		res.set("Content-Type", "application/json");
		AlpacaResp["ServerTransactionID"] = nTransactionID;
		if(strlen(ClientID))
			AlpacaResp["ClientID"] = uint32_t(atoi(ClientID)<0?0:atoi(ClientID));
		if(strlen(ClientTransactionID))
			AlpacaResp["ClientTransactionID"] = uint32_t(atoi(ClientTransactionID)<0?0:atoi(ClientTransactionID));
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

	res.print(sResp);
	res.flush(); 
}

void Slaved(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	String sResp;
	char ClientID[64];
	char ClientTransactionID[64];
	
	DBPrintln("Slaved");
	req.query("ClientID", ClientID, 64);
	req.query("ClientTransactionID", ClientTransactionID, 64);
	DBPrintln("ClientID : " + String(ClientID));
	DBPrintln("ClientTransactionID : " + String(ClientTransactionID));

	res.set("Content-Type", "application/json");
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	if(strlen(ClientID))
		AlpacaResp["ClientID"] = uint32_t(atoi(ClientID)<0?0:atoi(ClientID));
	if(strlen(ClientTransactionID))
		AlpacaResp["ClientTransactionID"] = uint32_t(atoi(ClientTransactionID)<0?0:atoi(ClientTransactionID));
	AlpacaResp["ErrorNumber"] = 1024;
	AlpacaResp["ErrorMessage"] = "Can't slave";
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.print(sResp);
	res.flush(); 
}

void getSlewing(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	String sResp;
	char ClientID[64];
	char ClientTransactionID[64];
	
	DBPrintln("getSlewing");
	req.query("ClientID", ClientID, 64);
	req.query("ClientTransactionID", ClientTransactionID, 64);
	DBPrintln("ClientID : " + String(ClientID));
	DBPrintln("ClientTransactionID : " + String(ClientTransactionID));

	res.set("Content-Type", "application/json");
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	if(strlen(ClientID))
		AlpacaResp["ClientID"] = uint32_t(atoi(ClientID)<0?0:atoi(ClientID));
	if(strlen(ClientTransactionID))
		AlpacaResp["ClientTransactionID"] = uint32_t(atoi(ClientTransactionID)<0?0:atoi(ClientTransactionID));
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

	res.print(sResp);
	res.flush(); 
}

void doAbort(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	String sResp;
	char ClientID[64];
	char ClientTransactionID[64];
	
	DBPrintln("doAbort");
	req.query("ClientID", ClientID, 64);
	req.query("ClientTransactionID", ClientTransactionID, 64);
	DBPrintln("ClientID : " + String(ClientID));
	DBPrintln("ClientTransactionID : " + String(ClientTransactionID));

	res.set("Content-Type", "application/json");
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	if(strlen(ClientID))
		AlpacaResp["ClientID"] = uint32_t(atoi(ClientID)<0?0:atoi(ClientID));
	if(strlen(ClientTransactionID))
		AlpacaResp["ClientTransactionID"] = uint32_t(atoi(ClientTransactionID)<0?0:atoi(ClientTransactionID));
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	Rotator->Stop();
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.print(sResp);
	res.flush(); 
}

void doCloseShutter(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	String sResp;
	char ClientID[64];
	char ClientTransactionID[64];
	String sTmpString = String(CLOSE_SHUTTER);

	DBPrintln("doCloseShutter");
	req.query("ClientID", ClientID, 64);
	req.query("ClientTransactionID", ClientTransactionID, 64);
	DBPrintln("ClientID : " + String(ClientID));
	DBPrintln("ClientTransactionID : " + String(ClientTransactionID));
#ifndef USE_WIFI
	res.set("Content-Type", "application/json");
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	if(strlen(ClientID))
		AlpacaResp["ClientID"] = uint32_t(atoi(ClientID)<0?0:atoi(ClientID));
	if(strlen(ClientTransactionID))
		AlpacaResp["ClientTransactionID"] = uint32_t(atoi(ClientTransactionID)<0?0:atoi(ClientTransactionID));
	AlpacaResp["ErrorNumber"] = 1024;
	AlpacaResp["ErrorMessage"] = "Not implemented";
#else
	if(!nbWiFiClient) {
		res.set("Content-Type", "application/json");
		AlpacaResp["ServerTransactionID"] = nTransactionID;
		if(strlen(ClientID))
			AlpacaResp["ClientID"] = uint32_t(atoi(ClientID)<0?0:atoi(ClientID));
		if(strlen(ClientTransactionID))
			AlpacaResp["ClientTransactionID"] = uint32_t(atoi(ClientTransactionID)<0?0:atoi(ClientTransactionID));
		AlpacaResp["ErrorNumber"] = 1035;
		AlpacaResp["ErrorMessage"] = "Shutter not connected";
	} else {
		res.set("Content-Type", "application/json");
		AlpacaResp["ServerTransactionID"] = nTransactionID;
		if(strlen(ClientID))
			AlpacaResp["ClientID"] = uint32_t(atoi(ClientID)<0?0:atoi(ClientID));
		if(strlen(ClientTransactionID))
			AlpacaResp["ClientTransactionID"] = uint32_t(atoi(ClientTransactionID)<0?0:atoi(ClientTransactionID));
		AlpacaResp["ErrorNumber"] = 0;
		AlpacaResp["ErrorMessage"] = "";
		shutterClient.print(sTmpString+ "#");
		ReceiveWiFi(shutterClient);
	}
#endif
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.print(sResp);
	res.flush(); 
}

void doFindHome(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	String sResp;
	char ClientID[64];
	char ClientTransactionID[64];
	
	DBPrintln("doFindHome");
	req.query("ClientID", ClientID, 64);
	req.query("ClientTransactionID", ClientTransactionID, 64);
	DBPrintln("ClientID : " + String(ClientID));
	DBPrintln("ClientTransactionID : " + String(ClientTransactionID));

	if(bLowShutterVoltage) {
		res.set("Content-Type", "application/json");
		AlpacaResp["ErrorNumber"] = 1032;
		AlpacaResp["ErrorMessage"] = "Low shutter voltage, staying at park position";
		serializeJson(AlpacaResp, sResp);
		res.print(sResp);
		res.flush();
		return;
	}

	res.set("Content-Type", "application/json");
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	if(strlen(ClientID))
		AlpacaResp["ClientID"] = uint32_t(atoi(ClientID)<0?0:atoi(ClientID));
	if(strlen(ClientTransactionID))
		AlpacaResp["ClientTransactionID"] = uint32_t(atoi(ClientTransactionID)<0?0:atoi(ClientTransactionID));
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	Rotator->StartHoming();
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.print(sResp);
	res.flush(); 
}

void doOpenShutter(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	String sResp;
	char ClientID[64];
	char ClientTransactionID[64];
	String sTmpString = String(OPEN_SHUTTER);

	DBPrintln("doOpenShutter");
	req.query("ClientID", ClientID, 64);
	req.query("ClientTransactionID", ClientTransactionID, 64);
	DBPrintln("ClientID : " + String(ClientID));
	DBPrintln("ClientTransactionID : " + String(ClientTransactionID));

#ifndef USE_WIFI
	res.set("Content-Type", "application/json");
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	if(strlen(ClientID))
		AlpacaResp["ClientID"] = uint32_t(atoi(ClientID)<0?0:atoi(ClientID));
	if(strlen(ClientTransactionID))
		AlpacaResp["ClientTransactionID"] = uint32_t(atoi(ClientTransactionID)<0?0:atoi(ClientTransactionID));
	AlpacaResp["ErrorNumber"] = 1024;
	AlpacaResp["ErrorMessage"] = "Not implemented";
#else
	if(!nbWiFiClient) {
		res.set("Content-Type", "application/json");
		AlpacaResp["ServerTransactionID"] = nTransactionID;
		if(strlen(ClientID))
			AlpacaResp["ClientID"] = uint32_t(atoi(ClientID)<0?0:atoi(ClientID));
		if(strlen(ClientTransactionID))
			AlpacaResp["ClientTransactionID"] = uint32_t(atoi(ClientTransactionID)<0?0:atoi(ClientTransactionID));
		AlpacaResp["ErrorNumber"] = 1035;
		AlpacaResp["ErrorMessage"] = "Shutter not connected";
	}
	else if(bLowShutterVoltage) {
		res.set("Content-Type", "application/json");
		AlpacaResp["ErrorNumber"] = 1032;
		AlpacaResp["ErrorMessage"] = "Low shutter voltage, staying at park position";
		serializeJson(AlpacaResp, sResp);
		res.print(sResp);
		res.flush();
	}
	else {
		res.set("Content-Type", "application/json");
		AlpacaResp["ServerTransactionID"] = nTransactionID;
		if(strlen(ClientID))
			AlpacaResp["ClientID"] = uint32_t(atoi(ClientID)<0?0:atoi(ClientID));
		if(strlen(ClientTransactionID))
			AlpacaResp["ClientTransactionID"] = uint32_t(atoi(ClientTransactionID)<0?0:atoi(ClientTransactionID));
		AlpacaResp["ErrorNumber"] = 0;
		AlpacaResp["ErrorMessage"] = "";

		shutterClient.print(sTmpString+ "#");
		ReceiveWiFi(shutterClient);
	}
#endif
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.print(sResp);
	res.flush(); 
}

void doPark(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	String sResp;
	double fParkPos;
	char ClientID[64];
	char ClientTransactionID[64];
	
	DBPrintln("doPark");
	req.query("ClientID", ClientID, 64);
	req.query("ClientTransactionID", ClientTransactionID, 64);
	DBPrintln("ClientID : " + String(ClientID));
	DBPrintln("ClientTransactionID : " + String(ClientTransactionID));

	res.set("Content-Type", "application/json");
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	if(strlen(ClientID))
		AlpacaResp["ClientID"] = uint32_t(atoi(ClientID)<0?0:atoi(ClientID));
	if(strlen(ClientTransactionID))
		AlpacaResp["ClientTransactionID"] = uint32_t(atoi(ClientTransactionID)<0?0:atoi(ClientTransactionID));
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";

	fParkPos = Rotator->GetParkAzimuth();
	Rotator->GoToAzimuth(fParkPos);
	bParked = true;

	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.print(sResp);
	res.flush(); 
}

void setPark(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	String sResp;
	double fParkPos;

	DBPrintln("setPark");
	FormData = formDataToJson(req);
#ifdef DEBUG
	serializeJson(FormData, sResp);
	DBPrintln("FormData : " + sResp);
	DBPrintln("FormData.size() : " + String(FormData.size()));
	sResp="";
#endif

	fParkPos = Rotator->GetAzimuth();
	Rotator->SetParkAzimuth(fParkPos);

	res.set("Content-Type", "application/json");
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	if(FormData["ClientID"])
		AlpacaResp["ClientID"] = uint32_t(FormData["ClientID"]);
	if(FormData["ClientTransactionID"])
		AlpacaResp["ClientTransactionID"] = uint32_t(FormData["ClientTransactionID"]);
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.print(sResp);
	res.flush();
}

void doAltitudeSlew(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	String sResp;

	DBPrintln("doAltitudeSlew");
	FormData = formDataToJson(req);
#ifdef DEBUG
	serializeJson(FormData, sResp);
	DBPrintln("FormData : " + sResp);
	DBPrintln("FormData.size() : " + String(FormData.size()));
	sResp="";
#endif
#ifndef USE_WIFI
	if(FormData.size()==0){
		res.set("Content-Type", "application/json");
		res.sendStatus(400);
		AlpacaResp["ErrorNumber"] = 400;
		AlpacaResp["ErrorMessage"] = "Invalid parameters";
		serializeJson(AlpacaResp, sResp);
		res.print(sResp);
		res.flush();
		return;
	}

	if(!FormData["Altitude"]) {
		res.set("Content-Type", "application/json");
		res.sendStatus(400);
		AlpacaResp["ErrorNumber"] = 400;
		AlpacaResp["ErrorMessage"] = "Invalid value";
		if(FormData["ClientID"])
			AlpacaResp["ClientID"] = uint32_t(FormData["ClientID"]);
		if(FormData["ClientTransactionID"])
			AlpacaResp["ClientTransactionID"] = uint32_t(FormData["ClientTransactionID"]);
		serializeJson(AlpacaResp, sResp);
		res.print(sResp);
		res.flush();
		return;
	}

	res.set("Content-Type", "application/json");
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	if(FormData["ClientID"])
		AlpacaResp["ClientID"] = uint32_t(FormData["ClientID"]);
	if(FormData["ClientTransactionID"])
		AlpacaResp["ClientTransactionID"] = uint32_t(FormData["ClientTransactionID"]);
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.print(sResp);
	res.flush();
#else
		res.set("Content-Type", "application/json");
		AlpacaResp["ErrorNumber"] = 1024;
		AlpacaResp["ErrorMessage"] = "Invalid method";
		serializeJson(AlpacaResp, sResp);
		res.print(sResp);
		res.flush();
		return;
#endif

}

void doGoTo(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	String sResp;
	double dNewPos;

	DBPrintln("doGoTo");
	FormData = formDataToJson(req);
#ifdef DEBUG
	serializeJson(FormData, sResp);
	DBPrintln("FormData : " + sResp);
	DBPrintln("FormData.size() : " + String(FormData.size()));
	sResp="";
#endif

	if(FormData.size()==0){
		res.set("Content-Type", "application/json");
		res.sendStatus(400);
		AlpacaResp["ErrorNumber"] = 400;
		AlpacaResp["ErrorMessage"] = "Invalid parameters";
		serializeJson(AlpacaResp, sResp);
		res.print(sResp);
		res.flush();
		return;
	}

	if(bLowShutterVoltage) {
		res.set("Content-Type", "application/json");
		AlpacaResp["ErrorNumber"] = 1032;
		AlpacaResp["ErrorMessage"] = "Low shutter voltage, staying at park position";
		AlpacaResp["ServerTransactionID"] = nTransactionID;
		if(FormData["ClientID"])
			AlpacaResp["ClientID"] = uint32_t(FormData["ClientID"]);
		if(FormData["ClientTransactionID"])
			AlpacaResp["ClientTransactionID"] = uint32_t(FormData["ClientTransactionID"]);
		serializeJson(AlpacaResp, sResp);
		res.print(sResp);
		res.flush();
		return;
	}

	if(!FormData["Azimuth"]) {
		res.set("Content-Type", "application/json");
		res.sendStatus(400);
		AlpacaResp["ErrorNumber"] = 400;
		AlpacaResp["ErrorMessage"] = "Invalid parameters";
		AlpacaResp["ServerTransactionID"] = nTransactionID;
		if(FormData["ClientID"])
			AlpacaResp["ClientID"] = uint32_t(FormData["ClientID"]);
		if(FormData["ClientTransactionID"])
			AlpacaResp["ClientTransactionID"] = uint32_t(FormData["ClientTransactionID"]);
		serializeJson(AlpacaResp, sResp);
		res.print(sResp);
		res.flush();
		return;
	}

	dNewPos = FormData["Azimuth"];
	if(dNewPos < 0 || dNewPos>360) {
		res.set("Content-Type", "application/json");
		AlpacaResp["ErrorNumber"] = 1025;
		AlpacaResp["ErrorMessage"] = "Invalid azimuth";
		AlpacaResp["ServerTransactionID"] = nTransactionID;
		if(FormData["ClientTransactionID"])
			AlpacaResp["ClientTransactionID"] = uint32_t(FormData["ClientTransactionID"]);
		serializeJson(AlpacaResp, sResp);
		res.print(sResp);
		res.flush();
		return;
	}

	Rotator->GoToAzimuth(dNewPos);

	res.set("Content-Type", "application/json");
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	if(FormData["ClientID"])
		AlpacaResp["ClientID"] = uint32_t(FormData["ClientID"]);
	if(FormData["ClientTransactionID"])
		AlpacaResp["ClientTransactionID"] = uint32_t(FormData["ClientTransactionID"]);
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.print(sResp);
	res.flush();
}

void doSyncAzimuth(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	String sResp;
	double dNewPos;

	DBPrintln("doSyncAzimuth");
	FormData = formDataToJson(req);
#ifdef DEBUG
	serializeJson(FormData, sResp);
	DBPrintln("FormData : " + sResp);
	DBPrintln("FormData.size() : " + String(FormData.size()));
	sResp="";
#endif

	if(FormData.size()==0){
		res.set("Content-Type", "application/json");
		res.sendStatus(400);
		AlpacaResp["ErrorNumber"] = 400;
		AlpacaResp["ErrorMessage"] = "Invalid parameters";
		AlpacaResp["ServerTransactionID"] = nTransactionID;
		serializeJson(AlpacaResp, sResp);
		res.print(sResp);
		res.flush();
		return;
	}

	if(!FormData["Azimuth"]) {
		res.set("Content-Type", "application/json");
		res.sendStatus(400);
		AlpacaResp["ErrorNumber"] = 1025;
		AlpacaResp["ErrorMessage"] = "Invalid azimuth";
		AlpacaResp["ServerTransactionID"] = nTransactionID;
		if(FormData["ClientID"])
			AlpacaResp["ClientID"] = uint32_t(FormData["ClientID"]);
		if(FormData["ClientTransactionID"])
			AlpacaResp["ClientTransactionID"] = uint32_t(FormData["ClientTransactionID"]);
		serializeJson(AlpacaResp, sResp);
		res.print(sResp);
		res.flush();
		return;
	}

	dNewPos = FormData["Azimuth"];
	if(dNewPos<0 || dNewPos > 360) {
		res.set("Content-Type", "application/json");
		AlpacaResp["ErrorNumber"] = 1025;
		AlpacaResp["ErrorMessage"] = "Invalid Azimuth";
		AlpacaResp["ServerTransactionID"] = nTransactionID;
		if(FormData["ClientID"])
			AlpacaResp["ClientID"] = uint32_t(FormData["ClientID"]);
		if(FormData["ClientTransactionID"])
			AlpacaResp["ClientTransactionID"] = uint32_t(FormData["ClientTransactionID"]);
		serializeJson(AlpacaResp, sResp);
		res.print(sResp);
		res.flush();
		return;
	}

	Rotator->SyncPosition(dNewPos);

	res.set("Content-Type", "application/json");
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	if(FormData["ClientID"])
		AlpacaResp["ClientID"] = uint32_t(FormData["ClientID"]);
	if(FormData["ClientTransactionID"])
		AlpacaResp["ClientTransactionID"] = uint32_t(FormData["ClientTransactionID"]);
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.print(sResp);
	res.flush();
}


void doSetup(Request &req, Response &res)
{
	JsonDocument FormData;
	String sResp;
	String sHTML;

	res.set("Content-Type", "text/html");

	DBPrintln("doSetup");
	FormData = formDataToJson(req);
#ifdef DEBUG
	serializeJson(FormData, sResp);
	DBPrintln("FormData : " + sResp);
	DBPrintln("FormData.size() : " + String(FormData.size()));
#endif
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
	EthernetClient client = mRestServer->available();
	if (client.connected()) {
		m_AlpacaRestServer->process(&client);
		client.stop();
		nTransactionID++;
  }

}
