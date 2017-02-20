#include <string.h>
#include <null.h>
#include "wimon.h"

struct link links[NLINKS];
struct scan scans[NSCANS];
int nlinks;
int nscans;

struct link* find_link_slot(int ifi)
{
	struct link* ls;

	for(ls = links; ls < links + nlinks; ls++)
		if(ls->ifi == ifi)
			return ls;

	return NULL;
}

struct link* grab_link_slot(int ifi)
{
	struct link* ls;
	
	if((ls = find_link_slot(ifi)))
		return ls;

	for(ls = links; ls < links + nlinks; ls++)
		if(!ls->ifi)
			return ls;
	if(ls >= links + NLINKS)
		return NULL;

	nlinks++;
	return ls;
}

void free_link_slot(struct link* ls)
{
	memset(ls, 0, sizeof(*ls));
}

struct scan* grab_scan_slot(int ifi, uint8_t* bssid)
{
	struct scan* sc;

	for(sc = scans; sc < scans + nscans; sc++)
		if(sc->ifi != ifi)
			continue;
		else if(memcmp(sc->bssid, bssid, 6))
			continue;
		else break;

	if(sc >= scans + NSCANS)
		return NULL;
	if(sc == scans + nscans)
		nscans++;

	return sc;
}

void free_scan_slot(struct scan* sc)
{
	memset(sc, 0, sizeof(*sc));
}

void drop_stale_scan_slots(int ifi, int seq)
{
	struct scan* sc;

	for(sc = scans; sc < scans + nscans; sc++)
		if(sc->ifi != ifi)
			continue;
		else if(sc->seq == seq)
			continue;
		else
			free_scan_slot(sc);
}
