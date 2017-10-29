#include <string.h>
#include <format.h>
#include <util.h>

#include "wsupp.h"

struct conn conns[NCONNS];
struct scan scans[NSCANS];
int nconns;
int nscans;

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

struct conn* grab_conn_slot(void)
{
	return grab_slot(conns, &nconns, NCONNS, sizeof(*conns));
}

void free_conn_slot(struct conn* cn)
{
	free_slot(conns, &nconns, sizeof(*cn), cn);
}

struct scan* find_scan_slot(byte bssid[6])
{
	struct scan* sc;

	for(sc = scans; sc < scans + nscans; sc++)
		if(!sc->freq)
			continue;
		else if(!memcmp(sc->bssid, bssid, 6))
			return sc;

	return NULL;
}

struct scan* grab_scan_slot(byte bssid[6])
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
