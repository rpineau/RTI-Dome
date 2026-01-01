//
// Firmware configuration
// Copyright © 2025 Rodolphe Pineau. All rights reserved.
//
#ifndef __R_CONFIG__
#define __R_CONFIG__
// #define DEBUG   // enable debug to serial port defined as DebugPort

#ifdef DEBUG
#pragma message "Debug messages enabled"
#define DebugPort Serial1    //  Rx2,Tx2 =  Serial1
#define DBPrint(x) if(DebugPort) DebugPort.print(x)
#define DBPrintln(x) if(DebugPort) DebugPort.println(x)
#define DBPrintHex(x) if(DebugPort) DebugPort.print(x, HEX)
#else
#pragma message "Debug messages disabled"
#define DBPrint(x)
#define DBPrintln(x)
#define DBPrintHex(x)
#endif // DEBUG

#define VERSION "2.645"

#define USE_ALPACA
#define USE_OTA_UPDATE
// if uncommented, USE_WIFI will enable all code related to the shutter over WiFi.
// This is useful for people who only want to automate the rotation.
#define USE_WIFI

#define Computer Serial     // USB = Serial

//
// ESP32 dev boards
//
// input
#define HOME_PIN            15  // Also used for Shutter open status
#define SPARE_OPENED_PIN 	33	// Digital Input, shutter open limit swith, not used on rotator board, spare input.
#define BUTTON_CCW          27 // Digital Input
#define BUTTON_CW           14 // Digital Input
#define CONDITION_SENSOR_PIN     25  // Digital Input from RG11 and other similar devices
#define SPARE1				34
#define SPARE2				26
// ouput
#define STEPPER_ENABLE_PIN  13  // Digital Output
#define DIRECTION_PIN        2  // Digital Output
#define STEP_PIN            32  // Digital Output
#define SPARE_OUT1			 0
#define SPARE_OUT2			12

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

#define MAX_SPEED           8000
#define ACCELERATION        7000

#define DEBOUNCE_TIME		50
/*
Micro-steps per rotation with a 200 step per rotation stepper and 15.3:1 gearbox
	NexDome 2m      : 440640
	Explora-Dome 8' : 479800
*/

#define STEPS_DEFAULT       440640

#define ETHERNET_CS     5
#define ETHERNET_INT	0
#define ETHERNET_RESET  4
#define CMD_SERVER_PORT 2323
#define domeEthernet Ethernet

#ifdef USE_WIFI
#define SHUTTER_PORT 2424
#define shutterWiFi WiFi
#endif

#ifdef USE_OTA_UPDATE
#define OTA_PORT	8080
#endif

#endif