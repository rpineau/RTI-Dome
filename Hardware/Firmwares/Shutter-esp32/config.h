//
// Firmware configuration
// Copyright © 2025 Rodolphe Pineau. All rights reserved.
//
#ifndef __R_CONFIG__
#define __R_CONFIG__
// #define DEBUG   // enable debug to serial port defined as DebugPort


#ifdef DEBUG
#pragma message "Debug messages enabled"
#define DebugPort Serial    //  USB = Serial, Rx2,Tx2 =  Serial1
#define DBPrint(x) if(DebugPort) DebugPort.print(x)
#define DBPrintln(x) if(DebugPort) DebugPort.println(x)
#define DBPrintHex(x) if(DebugPort) DebugPort.print(x, HEX)
#else
#pragma message "Debug messages disabled"
#define DBPrint(x)
#define DBPrintln(x)
#define DBPrintHex(x)
#endif // DEBUG

#define VERSION "3.000"

#define USE_OTA_UPDATE

//
// ESP32 dev boards
//
// input
#define CLOSED_PIN				15 	// Digital Input
#define OPEN_PIN 				33	// Digital Input
#define BUTTON_CLOSE			14	// Digital Input
#define BUTTON_OPEN				27	// Digital Input
#define CONDITION_SENSOR_PIN	25  // Digital Input from RG11 ands other similar device. Might be use as a spare input on shutter board.

#define SPARE_IN1					34	// SPARE_IN1 or lower shutter closed , no pullup on this pin
#define SPARE_IN2					26	// SPARE_IN2 or lower shutter opened
// For ease of coding
#define LOWER_CLOSED_PIN			34	// lower shutter closed, no pullup on this pin
#define LOWER_OPENED_PIN			26	// lower shutter open

// output
#define STEPPER_ENABLE_PIN		13  // Digital Output
#define DIRECTION_PIN	 		2  // Digital Output
#define STEP_PIN				32  // Digital Output
// 100k to ground on GPIO 12, and 100k to 3.3V on GPIO 0
#define SPARE_OUT1			 	 0
#define SPARE_OUT2				12

// For ease of coding
#define LOWER_DIR			 	 0
#define LOWER_ENABLE		 12

// analog
#define VOLTAGE_MONITOR_PIN A0  // GPIO36 / SENSOR_VP
#define AD_REF      3.3f	//
#define RES_MULT    5.0f 	//
#define DEFAULT_CUT_OFF_VOLTS	1100 // 11.00 V


#define MOVE_NEGATIVE       -1
#define MOVE_NONE            0
#define MOVE_POSITIVE        1

// #define M_ENABLE    HIGH
// #define M_DISABLE   LOW
#define M_ENABLE    LOW
#define M_DISABLE   HIGH
// A4988
//#define M_ENABLE    LOW
//#define M_DISABLE   HIGH

#define MAX_SPEED		6400
#define ACCELERATION	7000

#define STEPS_DEFAULT	885000


// DM556T stepper controller min pulse width  = 2.5uS
// ISD02/04/08 stepper controller min pulse width = 5uS at 1600rev/s (8 microsteps).
// TB6600 Stepper controller min pulse width = 5uS
// set to a safer value for all controllers
#define MIN_PULSE_WIDTH 5

#define DEFAULT_WATCHDOG_INTERVAL	15000
#define MIN_WATCHDOG_INTERVAL       5000
#define MAX_WATCHDOG_INTERVAL       300000

#define BATTERY_CHECK_INTERVAL      60000   // check battery once a minute

#ifdef USE_OTA_UPDATE
#define OTA_PORT	8080
#endif


typedef struct WIFICONFIG {
	IPAddress       ip;
	String 			sSSID;
	String			sPassword;
} WIFIConfig;


typedef struct ShutterConfiguration {
	unsigned long   stepsPerStroke;
	int             acceleration;
	int             maxSpeed;
	bool            reversed;
	int             cutoffVolts;
	unsigned long   watchdogInterval;
	bool            bHasDropShutter;
	bool            bBottomShutterOpenFirst;
	WIFIConfig		wifiIpConfig;
} Configuration;

#endif