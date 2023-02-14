//
// RTI-Zone Dome Rotator firmware.
// Support Arduino DUE and RP2040
//

class RemoteShutterClass
{
public:
	// Todo: remove this if state becomes a string
	enum ShutterStates { OPEN, CLOSED, OPENING, CLOSING, ERROR };

	// TODO: See if these can all be strings
	// These have to be real data for communications reasos
	String state = "4"; // Cause we don't know until the shutter tells us.

	// These aren't used by Rotator so why bother converting them to numeric values?
	String	acceleration = "";
	String	elevation = "";
	String	OpenError = "";
	String	speed = "";
	String	reversed = "";
	String	stepsPerStroke = "";
	// ASCOM checks version and if it's blank then shutter doesn't exist
	String	version = "";
	String	volts = "";
	String	watchdogInterval = "90"; // set proper default.. just in case.
	String  panid = "0000";
	String  lowVoltStateOrRaining = "";
	RemoteShutterClass();
};

RemoteShutterClass::RemoteShutterClass()
{

}