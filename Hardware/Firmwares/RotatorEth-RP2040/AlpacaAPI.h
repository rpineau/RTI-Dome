// Alpaca API prototype functions
#include <aWOT.h>

enum ShutterStates { OPEN, CLOSED, OPENING, CLOSING, BOTTOM_OPEN, BOTTOM_CLOSED, BOTTOM_OPENING, BOTTOM_CLOSING, ERROR, FINISHING_OPEN, FINISHING_CLOSE };

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
    
    DBPrintln("getConfiguredDevice");
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
    
    DBPrintln("getConfiguredDevice");
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
    
    DBPrintln("getDescription");
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
    
    DBPrintln("getDescription");
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
    
    DBPrintln("getDescription");
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
    
    DBPrintln("getDescription");
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
    
    DBPrintln("getDescription");
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

void getSupportedAxtions(Request &req, Response &res)
{
    JsonDocument AlpacaResp;
    String sResp;
    char ClientID[64];
    char ClientTransactionID[64];
    
    DBPrintln("getSupportedAxtions");
    req.query("ClientID", ClientID, 64);
    req.query("ClientTransactionID", ClientTransactionID, 64);
    DBPrintln("ClientID : " + String(ClientID));
    DBPrintln("ClientTransactionID : " + String(ClientTransactionID));

    res.set("Content-Type", "application/json");
    AlpacaResp["Value"][0] = "";
    AlpacaResp["Value"][1] = "";
    AlpacaResp["Value"][2] = "";
    AlpacaResp["Value"][3] = "";
    AlpacaResp["Value"][4] = "";
    AlpacaResp["Value"][5] = "";
    AlpacaResp["Value"][6] = "";
    AlpacaResp["Value"][7] = "";
    AlpacaResp["Value"][8] = "";
    AlpacaResp["Value"][9] = "";
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
    sTmpString = String(STATE_SHUTTER);

    DBPrintln("getDescription");
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
    
    DBPrintln("getDescription");
    req.query("ClientID", ClientID, 64);
    req.query("ClientTransactionID", ClientTransactionID, 64);
    DBPrintln("ClientID : " + String(ClientID));
    DBPrintln("ClientTransactionID : " + String(ClientTransactionID));

    res.set("Content-Type", "application/json");
    AlpacaResp["ServerTransactionID"] = nTransactionID;
    AlpacaResp["ClientTransactionID"] = atoi(ClientTransactionID);
    AlpacaResp["ErrorNumber"] = 0;
    AlpacaResp["ErrorMessage"] = "Ok";

    if(String(Rotator->GetHomeStatus() == ATHOME) {
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