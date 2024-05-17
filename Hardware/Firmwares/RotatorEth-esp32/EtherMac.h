//
// Create MAC address from DUE unique ID
//

#ifndef __ETHER_MAC__
#define __ETHER_MAC__

//
// Create MAC address from unique ID
//

void getMacAddress(byte* macBuffer, uint32_t* uid)
{
	uint64_t nUUID = ESP.getEfuseMac();
	macBuffer[0] = 0x52;
	macBuffer[1] = 0x54;
	macBuffer[2] = 0x49;
	macBuffer[3] = (byte)(nUUID>>16);
	macBuffer[4] = (byte)(nUUID>>8);
	macBuffer[5] = (byte)(nUUID);
	DBPrintln("mac : " + String(nUUID, HEX));
}
#endif // __ETHER_MAC__

