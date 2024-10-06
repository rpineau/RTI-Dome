// Rotator commands
//
//  Copyright © 2018 Rodolphe Pineau. All rights reserved.
//

const char ABORT                        = 'a'; // Tell everything to STOP!
const char ETH_RECONFIG                 = 'b'; // reconfigure ethernet
const char CALIBRATE_ROTATOR            = 'c'; // Calibrate the dome
const char RESTORE_MOTOR_DEFAULT        = 'd'; // restore default values for motor control.
const char ACCELERATION_ROTATOR         = 'e'; // Get/Set stepper acceleration
const char ETH_MAC_ADDRESS              = 'f'; // get the MAC address.
const char GOTO_ROTATOR                 = 'g'; // Get dome azimuth or move to new position (GoT
const char HOME_ROTATOR                 = 'h'; // Home the dome
const char HOMEAZ_ROTATOR               = 'i'; // Get/Set home position
const char IP_ADDRESS                   = 'j'; // get/set the IP address
const char VOLTS_ROTATOR                = 'k'; // Get volts and get/set cutoff
const char PARKAZ_ROTATOR               = 'l'; // Get/Set park azimuth
const char SLEW_ROTATOR                 = 'm'; // Get Slewing status/direction
const char RAIN_ROTATOR_ACTION          = 'n'; // Get/Set action when rain sensor triggered (do nothing, home, park)
const char IS_SHUTTER_PRESENT           = 'o'; // check if the shutter has responded to pings
const char IP_SUBNET                    = 'p'; // get/set the ip subnet
const char PANID                        = 'q'; // get and set the XBEE PAN ID
const char SPEED_ROTATOR                = 'r'; // Get/Set step rate (speed)
const char SYNC_ROTATOR                 = 's'; // Sync to new Azimuth
const char STEPSPER_ROTATOR             = 't'; // Get/set Steps per rotation
const char IP_GATEWAY                   = 'u'; // get/set default gateway IP
const char VERSION_ROTATOR              = 'v'; // Get Firmware Version
const char IP_DHCP                      = 'w'; // get/set DHCP mode
										//'x' see bellow
const char REVERSED_ROTATOR             = 'y'; // Get/Set stepper reversed status
const char HOMESTATUS_ROTATOR           = 'z'; // Get homed status
const char INIT_XBEE                    = 'x'; // force a XBee reconfig
const char RAIN_SHUTTER                 = 'F'; // Get rain status (from client) or tell shutter it's raining (from Rotator)

// available A B J N S U W X Z
// Shutter commands
const char CLOSE_SHUTTER                    = 'C'; // Close shutter
const char SHUTTER_RESTORE_MOTOR_DEFAULT    = 'D'; // Restore default values for motor control.
const char ACCELERATION_SHUTTER             = 'E'; // Get/Set stepper acceleration
											// 'F' see above
//const char ELEVATION_SHUTTER              = 'G'; // Get/Set altitude TBD
const char HELLO                            = 'H'; // Let shutter know we're here
const char WATCHDOG_INTERVAL                = 'I'; // Tell shutter when to trigger the watchdog for communication loss with rotator
const char VOLTS_SHUTTER                    = 'K'; // Get volts and set cutoff voltage (close if bellow)
const char SHUTTER_PING                     = 'L'; // Shutter ping, uses to reset watchdog timer.
const char STATE_SHUTTER                    = 'M'; // Get shutter state
const char OPEN_SHUTTER                     = 'O'; // Open the shutter
const char POSITION_SHUTTER                 = 'P'; // Get step position
const char SHUTTER_PANID                    = 'Q'; // get and set the XBEE PAN ID
const char SPEED_SHUTTER                    = 'R'; // Get/Set step rate (speed)
const char STEPSPER_SHUTTER                 = 'T'; // Get/Set steps per stroke
const char VERSION_SHUTTER                  = 'V'; // Get version string
const char REVERSED_SHUTTER                 = 'Y'; // Get/Set stepper reversed status
