// Alpaca API prototype functions
#include <aWOT.h>

enum ShutterStates { OPEN, CLOSED, OPENING, CLOSING, BOTTOM_OPEN, BOTTOM_CLOSED, BOTTOM_OPENING, BOTTOM_CLOSING, ERROR, FINISHING_OPEN, FINISHING_CLOSE };
enum AlpacaShutterStates { A_OPEN=0, A_CLOSED, A_OPENING, A_CLOSING,  A_ERROR};

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
	AlpacaResp["Value"][0] ["UniqueID"]= sUID;
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
	char name[ALPACA_VAR_BUF_LEN];
	char value[ALPACA_VAR_BUF_LEN];

	DBPrintln("doAction");
	while(req.form(name, ALPACA_VAR_BUF_LEN, value, ALPACA_VAR_BUF_LEN)){
		DBPrintln("name : " + String(name));
		DBPrintln("value : " + String(value));
		FormData[name]=String(value);
	}
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
	AlpacaResp["ErrorMessage"] = "Ok";
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
	char name[ALPACA_VAR_BUF_LEN];
	char value[ALPACA_VAR_BUF_LEN];

	DBPrintln("doCommandBlind");
	while(req.form(name, ALPACA_VAR_BUF_LEN, value, ALPACA_VAR_BUF_LEN)){
		DBPrintln("name : " + String(name));
		DBPrintln("value : " + String(value));
		FormData[name]=String(value);
	}
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
	AlpacaResp["ErrorMessage"] = "Ok";
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
	char name[ALPACA_VAR_BUF_LEN];
	char value[ALPACA_VAR_BUF_LEN];

	DBPrintln("doCommandBool");
	while(req.form(name, ALPACA_VAR_BUF_LEN, value, ALPACA_VAR_BUF_LEN)){
		DBPrintln("name : " + String(name));
		DBPrintln("value : " + String(value));
		FormData[name]=String(value);
	}
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
	AlpacaResp["ErrorMessage"] = "Ok";
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
	char name[ALPACA_VAR_BUF_LEN];
	char value[ALPACA_VAR_BUF_LEN];

	DBPrintln("doCommandString");
	while(req.form(name, ALPACA_VAR_BUF_LEN, value, ALPACA_VAR_BUF_LEN)){
		DBPrintln("name : " + String(name));
		DBPrintln("value : " + String(value));
		FormData[name]=String(value);
	}
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
	AlpacaResp["ErrorMessage"] = "Ok";
	AlpacaResp["Value"] = "Ok";
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.print(sResp);
	res.flush();
}

void getConected(Request &req, Response &res)
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
	AlpacaResp["ErrorMessage"] = "Ok";
	AlpacaResp["Value"] = true;
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.print(sResp);
	res.flush();
}

void setConected(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	String sResp;
	char ClientID[64];
	char ClientTransactionID[64];
	
	DBPrintln("setConected");
	req.query("ClientID", ClientID, 64);
	req.query("ClientTransactionID", ClientTransactionID, 64);
	DBPrintln("ClientID : " + String(ClientID));
	DBPrintln("ClientTransactionID : " + String(ClientTransactionID));

	res.set("Content-Type", "application/json");
	AlpacaResp["ServerTransactionID"] = nTransactionID;
	AlpacaResp["ClientTransactionID"] = atoi(ClientTransactionID);
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "Ok";
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
	AlpacaResp["ErrorMessage"] = "Ok";
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
	AlpacaResp["ErrorMessage"] = "Ok";
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
	AlpacaResp["ErrorMessage"] = "Ok";
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
	AlpacaResp["ErrorMessage"] = "Ok";
	AlpacaResp["Value"]= 0;
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
	AlpacaResp["ErrorMessage"] = "Ok";
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
	AlpacaResp["ErrorMessage"] = "Ok";
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
	AlpacaResp["ErrorMessage"] = "Ok";

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
	AlpacaResp["ErrorMessage"] = "Ok";

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
	AlpacaResp["ErrorMessage"] = "Ok";

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
	AlpacaResp["ErrorMessage"] = "Ok";
	
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
	AlpacaResp["ErrorMessage"] = "Ok";
	
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
	AlpacaResp["ErrorMessage"] = "Ok";
	
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
	AlpacaResp["ErrorMessage"] = "Ok";
	
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
	AlpacaResp["ErrorMessage"] = "Ok";
	
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
	AlpacaResp["ErrorMessage"] = "Ok";
	
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
	AlpacaResp["ErrorMessage"] = "Ok";
	
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
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "Ok";
	
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
	AlpacaResp["ErrorMessage"] = "Ok";
	
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
	AlpacaResp["ErrorMessage"] = "Ok";

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
	AlpacaResp["ErrorNumber"] = 1;
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
	AlpacaResp["ErrorMessage"] = "Ok";
	
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

	Wireless.print(sTmpString+ "#");
	ReceiveWireless();

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

	Wireless.print(sTmpString+ "#");
	ReceiveWireless();

	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.print(sResp);
	res.flush(); 
}

void doPark(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	String sResp;
	float fParkPos;
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
	char name[ALPACA_VAR_BUF_LEN];
	char value[ALPACA_VAR_BUF_LEN];
	float fParkPos;

	DBPrintln("setPark");
	while(req.form(name, ALPACA_VAR_BUF_LEN, value, ALPACA_VAR_BUF_LEN)){
		DBPrintln("name : " + String(name));
		DBPrintln("value : " + String(value));
		FormData[name]=String(value);
	}
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
	AlpacaResp["ErrorMessage"] = "Ok";
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
	char name[ALPACA_VAR_BUF_LEN];
	char value[ALPACA_VAR_BUF_LEN];

	DBPrintln("doAltitudeSlew");
	while(req.form(name, ALPACA_VAR_BUF_LEN, value, ALPACA_VAR_BUF_LEN)){
		DBPrintln("name : " + String(name));
		DBPrintln("value : " + String(value));
		FormData[name]=String(value);
	}
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

	if(!FormData["Altitude"]) {
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
	AlpacaResp["ErrorMessage"] = "Ok";
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.print(sResp);
	res.flush();
}

void doGoTo(Request &req, Response &res)
{
	JsonDocument AlpacaResp;
	JsonDocument FormData;
	String sResp;
	char name[ALPACA_VAR_BUF_LEN];
	char value[ALPACA_VAR_BUF_LEN];
	float fNewPos;

	DBPrintln("doGoTo");
	while(req.form(name, ALPACA_VAR_BUF_LEN, value, ALPACA_VAR_BUF_LEN)){
		DBPrintln("name : " + String(name));
		DBPrintln("value : " + String(value));
		FormData[name]=String(value);
	}
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

	fNewPos = FormData["Azimuth"];
	Rotator->GoToAzimuth(fNewPos);

	res.set("Content-Type", "application/json");
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "Ok";
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
	char name[ALPACA_VAR_BUF_LEN];
	char value[ALPACA_VAR_BUF_LEN];
	float fNewPos;

	DBPrintln("doSyncAzimuth");
	while(req.form(name, ALPACA_VAR_BUF_LEN, value, ALPACA_VAR_BUF_LEN)){
		DBPrintln("name : " + String(name));
		DBPrintln("value : " + String(value));
		FormData[name]=String(value);
	}
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

	fNewPos = FormData["Azimuth"];
	Rotator->SyncPosition(fNewPos);

	res.set("Content-Type", "application/json");
	AlpacaResp["ErrorNumber"] = 0;
	AlpacaResp["ErrorMessage"] = "Ok";
	serializeJson(AlpacaResp, sResp);
	DBPrintln("sResp : " + sResp);

	res.print(sResp);
	res.flush();
}

