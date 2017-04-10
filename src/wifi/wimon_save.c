#include <sys/open.h>
#include <sys/read.h>
#include <sys/write.h>
#include <sys/close.h>
#include <sys/mmap.h>

#include <format.h>
#include <string.h>

#include "config.h"
#include "wimon.h"
#include "wimon_save.h"

void release_config(void)
{
	eprintf("%s\n", __FUNCTION__);
}

int load_link(struct link* ls)
{
	eprintf("%s %s\n", __FUNCTION__, ls->name);
	return 0;
}

void save_link(struct link* ls)
{
	eprintf("%s %s\n", __FUNCTION__, ls->name);
}

extern char* test_psk;
extern uint8_t test_ssid[];
extern int test_slen;

int saved_psk_prio(uint8_t* ssid, int slen)
{
	if(slen != test_slen)
		return -1;
	if(memcmp(ssid, test_ssid, slen))
		return -1;

	return 1;
}

int load_psk(uint8_t* ssid, int slen, char* psk, int plen)
{
	eprintf("%s %s\n", __FUNCTION__, ssid);

	if(plen < 2*32+1)
		return 0;

	if(slen != test_slen)
		return 0;
	if(memcmp(ssid, test_ssid, slen))
		return 0;

	memcpy(psk, test_psk, strlen(test_psk)+1);
	return 1;
}

void save_psk(uint8_t* ssid, int slen, char* psk, int plen)
{
	eprintf("%s %s\n", __FUNCTION__, ssid);
}
