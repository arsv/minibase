#include <string.h>
#include <format.h>
#include <null.h>

#include "wimon.h"

/* Slot allocation for various objects used in wimon. */

struct link links[NLINKS];
struct scan scans[NSCANS];
struct conn conns[NCONNS];
int nlinks;
int nscans;
int nconns;
struct child children[NCHILDREN];
int nchildren;

struct uplink uplink; /* single one for now */

/* Some time ago this was done in a more human manner similar to find_*
   functions below, but the code is effectively the same for all kinds
   of slots, so it has been merged.

   The following assumes an empty slot is always all-bits-0. */

static void* grab_slot(void* slots, int* count, int total, int size)
{
	void* ptr = slots + size*(*count);
	void* end = slots + size*total;
	void* p;

	for(p = slots; p < ptr; p += size)
		if(!nonzero(p, size))
			break;
	if(p < ptr)
		return p;
	if(p >= end)
		return NULL;

	*count += 1;
	return p;
}

static void free_slot(void* slots, int* count, int size, void* p)
{
	memzero(p, size);

	while(*count > 0) {
		p = slots + size*(*count - 1);

		if(nonzero(p, size))
			break;

		*count -= 1;
	}
}

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

	return grab_slot(links, &nlinks, NLINKS, sizeof(*ls));
}

void free_link_slot(struct link* ls)
{
	free_slot(links, &nlinks, sizeof(*ls), ls);
}

struct scan* find_scan_slot(uint8_t* bssid)
{
	struct scan* sc;

	for(sc = scans; sc < scans + nscans; sc++)
		if(!sc->freq)
			continue;
		else if(!memcmp(sc->bssid, bssid, 6))
			return sc;

	return NULL;
}

struct scan* grab_scan_slot(uint8_t* bssid)
{
	struct scan* sc;

	if((sc = find_scan_slot(bssid)))
		return sc;

	return grab_slot(scans, &nscans, NSCANS, sizeof(*sc));
}

void free_scan_slot(struct scan* sc)
{
	free_slot(scans, &nscans, sizeof(*sc), sc);
}

void drop_scan_slots()
{
	memzero(scans, nscans*sizeof(*scans));
	nscans = 0;
}

struct child* grab_child_slot(void)
{
	return grab_slot(children, &nchildren, NCHILDREN, sizeof(*children));
}

struct child* find_child_slot(int pid)
{
	struct child* ch;

	for(ch = children; ch < children + nchildren; ch++)
		if(ch->pid == pid)
			return ch;
	
	return NULL;
}

void free_child_slot(struct child* ch)
{
	free_slot(children, &nchildren, sizeof(*ch), ch);
}

struct conn* grab_conn_slot(void)
{
	return grab_slot(conns, &nconns, NCONNS, sizeof(*conns));
}

void free_conn_slot(struct conn* cn)
{
	free_slot(conns, &nconns, sizeof(*cn), cn);
}
