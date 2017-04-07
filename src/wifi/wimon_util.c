#include "wimon.h"

int ssidlen(uint8_t* ssid)
{
	int i;

	for(i = SSIDLEN; i > 0; i--)
		if(ssid[i-1])
			break;

	return i;
}
