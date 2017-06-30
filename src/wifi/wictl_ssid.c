#include <nlusctl.h>
#include <format.h>
#include <string.h>
#include <heap.h>
#include <util.h>
#include <fail.h>

#include "common.h"
#include "wictl.h"

/* Let the user pick a SSID from the scan list with either a partial
   patter, or a corresponding BSSID. This of course requires fetching
   the scan list before sending CMD_FIXEDAP, but on the other hand
   error reporting is much easier.

   SSIDs are not something we should ask the user to type in full. */

static int parse_bssid(char* key, uint8_t* bssid, int bslen)
{
	char* p;

	if(bslen < 6)
		return 0;
	if(!(p = parsemac(key, bssid)) || *p)
		return 0;

	return 6;
}

static int match_bssid(attr sc, uint8_t* bssid, int bslen)
{
	attr at;

	if(!(at = uc_sub(sc, ATTR_BSSID)))
		return 0;

	uint8_t* payload = uc_payload(at);
	int paylen = uc_paylen(at);
	int off = paylen - bslen;

	if(off < 0)
		return 0;
	if(memcmp(payload + off, bssid, bslen))
		return 0;

	return 1;
}

static attr find_by_bssid(attr* scans, uint8_t* bssid, int bslen)
{
	attr *sp, sc = NULL;

	for(sp = scans; *sp; sp++)
		if(!match_bssid(*sp, bssid, bslen))
			continue;
		else if(!sc)
			sc = *sp;
		else
			fail("ambiguous BSSID suffix", NULL, 0);
	if(!sc)
		fail("no matching BSSID in scan list", NULL, 0);

	return sc;
}

static int dotcmp(char* a, char* b, int len)
{
	for(; len-- > 0; a++, b++)
		if(*a == '.')
			continue;
		else if(*a != *b)
			return 1;

	return 0;
}

static int match_name(attr sc, char* name, int nlen)
{
	attr at;

	if(!(at = uc_sub(sc, ATTR_SSID)))
		return 0;
	if(uc_paylen(at) < nlen)
		return 0;
	if(dotcmp(name, at->payload, nlen))
		return 0;

	return 1;
}

static attr find_by_ssid(attr* scans, char* name)
{
	int nlen = strlen(name);
	attr *sp, sc = NULL;

	for(sp = scans; *sp; sp++)
		if(!match_name(*sp, name, nlen))
			continue;
		else if(!sc)
			sc = *sp;
		else
			fail("ambiguous SSID prefix", NULL, 0);
	if(!sc)
		fail("no APs with matching SSID in range", NULL, 0);

	return sc;
}

void find_ssid(attr* scans, char* key, void* ssid, int* slen, int* saved)
{
	uint8_t bssid[6];
	int bslen;
	attr sc, at;
	int* prio;

	if((bslen = parse_bssid(key, bssid, sizeof(bssid))))
		sc = find_by_bssid(scans, bssid, bslen);
	else
		sc = find_by_ssid(scans, key);

	if(!(at = uc_sub(sc, ATTR_SSID)))
		fail("invalid scan data", NULL, 0);
	if(uc_paylen(at) > *slen)
		fail("matching SSID too long", NULL, 0);

	*slen = uc_paylen(at);
	memcpy(ssid, uc_payload(at), uc_paylen(at));

	if(!(prio = uc_sub_int(sc, ATTR_PRIO)))
		*saved = 0;
	else if(*prio >= 0)
		*saved = 1;
	else
		*saved = 0;
}
