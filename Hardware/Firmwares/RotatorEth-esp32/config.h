//
// Firmware configuration
// Copyright © 2025 Rodolphe Pineau. All rights reserved.
//
#ifndef __R_CONFIG__
#define __R_CONFIG__
#define DEBUG   // enable debug to serial port defined as DebugPort

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
#define MAX_TIMEOUT 10

#define USE_EXT_EEPROM
#define USE_ETHERNET
#define USE_ALPACA
#define USE_OTA_UPDATE
// if uncommented, USE_WIFI will enable all code related to the shutter over WiFi.
// This is useful for people who only want to automate the rotation.
#define USE_WIFI

#define Computer Serial     // USB = Serial

#define I2C_WIRE    Wire

#define EEPROM_ADDR 0x50
#define I2C_CHUNK_SIZE  16

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

// analog
#define VOLTAGE_MONITOR_PIN A0  // GPIO26/ADC0
#define AD_REF      3.3f
#define RES_MULT    5.0f // resistor voltage divider on the shield


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

/*
Micro-steps per rotation with or 200 step per rotaton stepper and 15.3:1 gearbox
	NexDome 2m      : 440640
	Explora-Dome 8' : 479800
*/

#define STEPS_DEFAULT       440640

// DM556T stepper controller min pulse width  = 2.5uS
// #define MIN_PULSE_WIDTH 3

// ISD02/04/08 stepper controller min pulse width = 5uS at 1600rev/s (8 microsteps).
// TB6600 Stepper controller min pulse width = 5uS
#define MIN_PULSE_WIDTH 5

// used to offset the config location.. at some point.
#define EEPROM_LOCATION     0  // not used with Arduino Due flash
#define EEPROM_SIGNATURE    0001

#define WIFI_VAR_LEN 64
#ifdef USE_OTA_UPDATE
#define OTA_PORT	8080
#endif
#ifdef USE_ETHERNET
#define ETHERNET_CS     5
#define ETHERNET_INT	0
#define ETHERNET_RESET  4
#define CMD_SERVER_PORT 2323
#define domeEthernet Ethernet
#endif // USE_ETHERNET

#ifdef USE_WIFI
#define SHUTTER_PORT 2424
#define shutterWiFi WiFi
#endif

#endif