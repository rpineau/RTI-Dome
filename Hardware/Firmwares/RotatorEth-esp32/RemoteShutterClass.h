//
// RTI-Zone Dome Rotator firmware.
//
//  Copyright © 2024 Rodolphe Pineau. All rights reserved.
//
//

class RemoteShutterClass
{
public:
	enum ShutterStates { OPEN, CLOSED, OPENING, CLOSING, BOTTOM_OPEN, BOTTOM_CLOSED, BOTTOM_OPENING, BOTTOM_CLOSING, ERROR, FINISHING_OPEN, FINISHING_CLOSE };
	enum shutterOrder {BOTTOM_FIRST, TOP_FIRST};

	int state = ERROR; // Cause we don't know until the shutter tells us.

	int		acceleration = 0;
	float	elevation = 0.0f;
	String	OpenError = "";
	int		speed = 0;
	bool	reversed = "";
	int		stepsPerStroke = 0;
	String	version = "";
	int	volts = 0;
	int	voltsCutOff = 0;
	unsigned int watchdogInterval = 90; // set proper default.. just in case.
	String  ssid = "RTIShutter";
	String  lowVoltStateOrBadConditions = "";
	RemoteShutterClass();
};

RemoteShutterClass::RemoteShutterClass()
{

}
