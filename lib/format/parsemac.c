#include <format.h>
#include <null.h>

char* parsemac(char* p, uint8_t* mac)
{
	int i;

	for(i = 0; i < 6; i++) {
		if(!(p = parsebyte(p, &mac[i])))
			return p;
		if(i == 5)
			break;
		else if(*p != ':')
			return NULL;
		else
			p++;
	}

	return p;
}

