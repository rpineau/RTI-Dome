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
	double	elevation = 0;
	String	OpenError = "";
	int		speed = 0;
	bool	reversed = "";
	int		stepsPerStroke = 0;
	// ASCOM checks version and if it's blank then shutter doesn't exist
	String	version = "";
	double	volts = 0.0;
	double	voltsCutOff = 0.0;
	unsigned int	watchdogInterval = 90; // set proper default.. just in case.
	String  panid = "0000";
	String  lowVoltStateOrRaining = "";
	RemoteShutterClass();
};

RemoteShutterClass::RemoteShutterClass()
{

}
