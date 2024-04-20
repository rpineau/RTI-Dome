// Alpaca API prototype functions
#include <aWOT.h>

#define ALPACA_VAR_BUF_LEN 256

enum ShutterStates { OPEN, CLOSED, OPENING, CLOSING, BOTTOM_OPEN, BOTTOM_CLOSED, BOTTOM_OPENING, BOTTOM_CLOSING, ERROR, FINISHING_OPEN, FINISHING_CLOSE };
enum AlpacaShutterStates { A_OPEN=0, A_CLOSED, A_OPENING, A_CLOSING,  A_ERROR};

volatile bool bAlpacaConnected = false;

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
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	AlpacaResp["ClientTransactionID"] = atoi(ClientTransactionID);
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
	
	DBPrintln("getDescription");
	req.query("ClientID", ClientID, 64);
	req.query("ClientTransactionID", ClientTransactionID, 64);
	DBPrintln("ClientID : " + String(ClientID));
	DBPrintln("ClientTransactionID : " + String(ClientTransactionID));

	res.set("Content-Type", "application/json");
	AlpacaResp["Value"]["ServerName"]= "RTIDome Alpaca";
	AlpacaResp["Value"]["Manufacturer"]= "RTI-Zone";
	AlpacaResp["Value"]["ManufacturerVersion"]= VERSION;
	AlpacaResp["Value"]["Location"]= "Earth";
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	AlpacaResp["ClientTransactionID"] = atoi(ClientTransactionID);
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
	
	DBPrintln("getConfiguredDevice");
	req.query("ClientID", ClientID, 64);
	req.query("ClientTransactionID", ClientTransactionID, 64);
	DBPrintln("ClientID : " + String(ClientID));
	DBPrintln("ClientTransactionID : " + String(ClientTransactionID));

	res.set("Content-Type", "application/json");
	AlpacaResp["Value"][0] ["DeviceName"]= "RTIDome";
	AlpacaResp["Value"][0] ["DeviceType"]= "dome";
	AlpacaResp["Value"][0] ["DeviceNumber"]= 0;
	AlpacaResp["Value"][0] ["UniqueID"]= uuid;
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	AlpacaResp["ClientTransactionID"] = atoi(ClientTransactionID);
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

	DBPrintln("doAction");
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

	DBPrintln("doCommandBlind");
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

	res.set("Content-Type", "application/json");
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
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

	DBPrintln("doCommandBool");
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

	res.set("Content-Type", "application/json");
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	AlpacaResp["Value"] = true;
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

	DBPrintln("doCommandString");
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

	res.set("Content-Type", "application/json");
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
	AlpacaResp["Value"] = "Ok";
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
	
	DBPrintln("getConected");
	req.query("ClientID", ClientID, 64);
	req.query("ClientTransactionID", ClientTransactionID, 64);
	DBPrintln("ClientID : " + String(ClientID));
	DBPrintln("ClientTransactionID : " + String(ClientTransactionID));

	res.set("Content-Type", "application/json");
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	AlpacaResp["ClientTransactionID"] = atoi(ClientTransactionID);
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
	char ClientID[64];
	char ClientTransactionID[64];

	DBPrintln("setConected");
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

	if(!FormData["Connected"]) {
		res.set("Content-Type", "application/json");
		res.sendStatus(400);
		AlpacaResp["ErrorNumber"] = 400;
		AlpacaResp["ErrorMessage"] = "Invalid parameters";
		serializeJson(AlpacaResp, sResp);
		res.print(sResp);
		res.flush();
		return;
	}

	bAlpacaConnected = (FormData["Connected"] == String("True"));
#ifdef DEBUG
		DBPrintln("bAlpacaConnected : " + (bAlpacaConnected?String("True"):String("False")));
#endif
	res.set("Content-Type", "application/json");
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
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
	
	DBPrintln("getDeviceDescription");
	req.query("ClientID", ClientID, 64);
	req.query("ClientTransactionID", ClientTransactionID, 64);
	DBPrintln("ClientID : " + String(ClientID));
	DBPrintln("ClientTransactionID : " + String(ClientTransactionID));

	res.set("Content-Type", "application/json");
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	AlpacaResp["ClientTransactionID"] = atoi(ClientTransactionID);
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
	
	DBPrintln("getDriverInfo");
	req.query("ClientID", ClientID, 64);
	req.query("ClientTransactionID", ClientTransactionID, 64);
	DBPrintln("ClientID : " + String(ClientID));
	DBPrintln("ClientTransactionID : " + String(ClientTransactionID));

	res.set("Content-Type", "application/json");
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	AlpacaResp["ClientTransactionID"] = atoi(ClientTransactionID);
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
	
	DBPrintln("getDriverVersion");
	req.query("ClientID", ClientID, 64);
	req.query("ClientTransactionID", ClientTransactionID, 64);
	DBPrintln("ClientID : " + String(ClientID));
	DBPrintln("ClientTransactionID : " + String(ClientTransactionID));

	res.set("Content-Type", "application/json");
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	AlpacaResp["ClientTransactionID"] = atoi(ClientTransactionID);
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
	
	DBPrintln("getInterfaceVersion");
	req.query("ClientID", ClientID, 64);
	req.query("ClientTransactionID", ClientTransactionID, 64);
	DBPrintln("ClientID : " + String(ClientID));
	DBPrintln("ClientTransactionID : " + String(ClientTransactionID));

	res.set("Content-Type", "application/json");
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	AlpacaResp["ClientTransactionID"] = atoi(ClientTransactionID);
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
	
	DBPrintln("getName");
	req.query("ClientID", ClientID, 64);
	req.query("ClientTransactionID", ClientTransactionID, 64);
	DBPrintln("ClientID : " + String(ClientID));
	DBPrintln("ClientTransactionID : " + String(ClientTransactionID));

	res.set("Content-Type", "application/json");
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	AlpacaResp["ClientTransactionID"] = atoi(ClientTransactionID);
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
	
	DBPrintln("getSupportedActions");
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
	AlpacaResp["ClientTransactionID"] = atoi(ClientTransactionID);
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

	DBPrintln("getAltitude");
	req.query("ClientID", ClientID, 64);
	req.query("ClientTransactionID", ClientTransactionID, 64);
	DBPrintln("ClientID : " + String(ClientID));
	DBPrintln("ClientTransactionID : " + String(ClientTransactionID));

	res.set("Content-Type", "application/json");
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	AlpacaResp["ClientTransactionID"] = atoi(ClientTransactionID);
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";

#ifndef STANDALONE
	Wireless.print(sTmpString + "#");
	ReceiveWireless();
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
	
	DBPrintln("geAtHome");
	req.query("ClientID", ClientID, 64);
	req.query("ClientTransactionID", ClientTransactionID, 64);
	DBPrintln("ClientID : " + String(ClientID));
	DBPrintln("ClientTransactionID : " + String(ClientTransactionID));

	res.set("Content-Type", "application/json");
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	AlpacaResp["ClientTransactionID"] = atoi(ClientTransactionID);
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
	
	DBPrintln("geAtPark");
	req.query("ClientID", ClientID, 64);
	req.query("ClientTransactionID", ClientTransactionID, 64);
	DBPrintln("ClientID : " + String(ClientID));
	DBPrintln("ClientTransactionID : " + String(ClientTransactionID));

	res.set("Content-Type", "application/json");
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	AlpacaResp["ClientTransactionID"] = atoi(ClientTransactionID);
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
	
	DBPrintln("getAzimuth");
	req.query("ClientID", ClientID, 64);
	req.query("ClientTransactionID", ClientTransactionID, 64);
	DBPrintln("ClientID : " + String(ClientID));
	DBPrintln("ClientTransactionID : " + String(ClientTransactionID));

	res.set("Content-Type", "application/json");
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	AlpacaResp["ClientTransactionID"] = atoi(ClientTransactionID);
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
	AlpacaResp["ClientTransactionID"] = atoi(ClientTransactionID);
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
	AlpacaResp["ClientTransactionID"] = atoi(ClientTransactionID);
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
	AlpacaResp["ClientTransactionID"] = atoi(ClientTransactionID);
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
	AlpacaResp["ClientTransactionID"] = atoi(ClientTransactionID);
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
	AlpacaResp["ClientTransactionID"] = atoi(ClientTransactionID);
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
	AlpacaResp["ClientTransactionID"] = atoi(ClientTransactionID);
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
	AlpacaResp["ClientTransactionID"] = atoi(ClientTransactionID);
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
	AlpacaResp["ClientTransactionID"] = atoi(ClientTransactionID);
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

	res.set("Content-Type", "application/json");
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	AlpacaResp["ClientTransactionID"] = atoi(ClientTransactionID);
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";

#ifndef STANDALONE
	Wireless.print(sTmpString + "#");
	ReceiveWireless();
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
#else
	AlpacaResp["Value"] = A_OPEN;
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
	AlpacaResp["ClientTransactionID"] = atoi(ClientTransactionID);
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
	AlpacaResp["ClientTransactionID"] = atoi(ClientTransactionID);
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
	AlpacaResp["ClientTransactionID"] = atoi(ClientTransactionID);
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

	res.set("Content-Type", "application/json");
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	AlpacaResp["ClientTransactionID"] = atoi(ClientTransactionID);
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";

#ifndef STANDALONE
	Wireless.print(sTmpString+ "#");
	ReceiveWireless();
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

	res.set("Content-Type", "application/json");
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	AlpacaResp["ClientTransactionID"] = atoi(ClientTransactionID);
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

	res.set("Content-Type", "application/json");
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	AlpacaResp["ClientTransactionID"] = atoi(ClientTransactionID);
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
#ifndef STANDALONE
	Wireless.print(sTmpString+ "#");
	ReceiveWireless();
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
	AlpacaResp["ClientTransactionID"] = atoi(ClientTransactionID);
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
#ifndef STANDALONE
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
		serializeJson(AlpacaResp, sResp);
		res.print(sResp);
		res.flush();
		return;
	}

	res.set("Content-Type", "application/json");
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
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

	if(!FormData["Azimuth"]) {
		res.set("Content-Type", "application/json");
		res.sendStatus(400);
		AlpacaResp["ErrorNumber"] = 400;
		AlpacaResp["ErrorMessage"] = "Invalid parameters";
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
		serializeJson(AlpacaResp, sResp);
		res.print(sResp);
		res.flush();
		return;
	}

	Rotator->GoToAzimuth(dNewPos);

	res.set("Content-Type", "application/json");
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
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
		serializeJson(AlpacaResp, sResp);
		res.print(sResp);
		res.flush();
		return;
	}

	Rotator->SyncPosition(dNewPos);

	res.set("Content-Type", "application/json");
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "";
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