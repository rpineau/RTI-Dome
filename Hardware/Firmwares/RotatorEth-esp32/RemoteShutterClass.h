//
// RTI-Zone Dome Rotator firmware.
//
//  Copyright © 2024 Rodolphe Pineau. All rights reserved.
//
//

class RemoteShutterClass
{
public:
	// Todo: remove this if state becomes a string
	enum ShutterStates { OPEN, CLOSED, OPENING, CLOSING, ERROR };

	// TODO: See if these can all be strings
	// These have to be real data for communications reasos
	int state = 4; // Cause we don't know until the shutter tells us.

	// These aren't used by Rotator so why bother converting them to numeric values?
	int		acceleration = 0;
	float	elevation = 0.0f;
	String	OpenError = "";
	int		speed = 0;
	bool	reversed = "";
	int		stepsPerStroke = 0;
	// ASCOM checks version and if it's blank then shutter doesn't exist
	String	version = "";
	float	volts = 0.0f;
	float	voltsCutOff = 0.0f;
	unsigned int	watchdogInterval = 90; // set proper default.. just in case.
	String  ssid = "RTIShutter";
	String  lowVoltStateOrBadConditions = "";
	RemoteShutterClass();
};

RemoteShutterClass::RemoteShutterClass()
{

}
