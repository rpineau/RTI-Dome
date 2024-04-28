//
// Create MAC address from DUE unique ID
//

#ifndef __ETHER_MAC__
#define __ETHER_MAC__

#include <hardware/flash.h>
void getMacAddress(byte* macBuffer, uint32_t* uid)
{
	uint8_t UniqueID[8];
	flash_get_unique_id(UniqueID);
	macBuffer[0] = 0x52;
	macBuffer[1] = 0x54;
	macBuffer[2] = 0x49;
	macBuffer[3] = (byte)(UniqueID[5]);
	macBuffer[4] = (byte)(UniqueID[6]);
	macBuffer[5] = (byte)(UniqueID[7]);
}
#endif // __ETHER_MAC__

