#include <bits/null.h>
#include <string.h>
#include <format.h>

#include "wimon.h"

/* Slot allocation for various objects used in wimon. */

struct link links[NLINKS];
struct scan scans[NSCANS];
struct conn conns[NCONNS];
struct addr addrs[NADDRS];
int nlinks;
int nscans;
int nconns;
int naddrs;

struct child children[NCHILDREN];
int nchildren;

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

/* Address tacking is almost entirely for reporting, wimon itself
   does not need to know these. Why it is still done: through requests
   (i.e. wimon doing a sync request to RTNL in ctrl handler) would cause
   lots of troubles in otherwise async RTNL code, and having wictl query
   RTNL independently just does not feel right. */

struct addr* get_addr(int ifi, int type, struct addr* prev)
{
	struct addr* ad;

	if(!prev)
		ad = addrs;
	else if(prev < addrs)
		return NULL;
	else
		ad = prev + 1;

	for(; ad < addrs + naddrs; ad++)
		if(ad->ifi == ifi && ad->type == type)
			return ad;

	return NULL;
}

static struct addr* find_addr(int ifi, int type, uint8_t* ip, int mask)
{
	struct addr* ad;

	for(ad = get_addr(ifi, type, NULL); ad; ad = get_addr(ifi, type, ad))
		if(memcmp(ad->ip, ip, 4))
			continue;
		else if(ad->mask != mask)
			continue;
		else return ad;

	return NULL;
}

void add_addr(int ifi, int type, uint8_t* ip, int mask)
{
	struct addr* ad;

	if((ad = find_addr(ifi, type, ip, mask)))
		return;
	if(!(ad = grab_slot(addrs, &naddrs, NADDRS, sizeof(*ad))))
		return;

	ad->ifi = ifi;
	ad->type = type;
	memcpy(ad->ip, ip, 4);
	ad->mask = mask;
}

static void free_addr_slot(struct addr* ad)
{
	free_slot(addrs, &naddrs, sizeof(*ad), ad);
}

void del_addr(int ifi, int type, uint8_t* ip, int mask)
{
	struct addr* ad;

	if((ad = find_addr(ifi, type, ip, mask)))
		free_addr_slot(ad);
}

void del_all_addrs(int ifi, int type)
{
	struct addr* ad = NULL;

	while((ad = get_addr(ifi, type, ad)))
		free_addr_slot(ad);
}
