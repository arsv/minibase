#include <string.h>
#include <format.h>

#include "ifmon.h"

/* Slot allocation for various objects used in wimon. */

struct link links[NLINKS];
struct conn conns[NCONNS];
struct dhcp dhcps[NDHCPS];
struct addr addrs[NADDRS];
int nlinks;
int nconns;
int ndhcps;
int naddrs;

struct proc procs[NPROCS];
int nprocs;

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

struct proc* grab_proc_slot(void)
{
	return grab_slot(procs, &nprocs, NPROCS, sizeof(*procs));
}

struct proc* find_proc_slot(int pid)
{
	struct proc* ch;

	for(ch = procs; ch < procs + nprocs; ch++)
		if(ch->pid == pid)
			return ch;
	
	return NULL;
}

void free_proc_slot(struct proc* ch)
{
	free_slot(procs, &nprocs, sizeof(*ch), ch);
}

struct conn* grab_conn_slot(void)
{
	return grab_slot(conns, &nconns, NCONNS, sizeof(*conns));
}

void free_conn_slot(struct conn* cn)
{
	free_slot(conns, &nconns, sizeof(*cn), cn);
}

struct dhcp* find_dhcp_slot(int ifi)
{
	struct dhcp* dh;

	for(dh = dhcps; dh < dhcps + ndhcps; dh++)
		if(dh->ifi == ifi)
			return dh;

	return NULL;
}

struct dhcp* grab_dhcp_slot(int ifi)
{
	struct dhcp* dh;

	if((dh = find_dhcp_slot(ifi)))
		return dh;

	return grab_slot(dhcps, &ndhcps, NDHCPS, sizeof(*dh));
}

void free_dhcp_slot(struct dhcp* dh)
{
	free_slot(dhcps, &ndhcps, sizeof(*dh), dh);
}

void flush_addrs(int ifi, int tag)
{
	struct addr* ad;

	for(ad = addrs; ad < addrs + naddrs; ad++) {
		if(ifi && ad->ifi != ifi)
			continue;
		if(tag && ad->tag != tag)
			continue;
		memzero(ad, sizeof(*ad));
	}
}

void record_addr(int ifi, byte tag, byte* ip, byte mask)
{
	struct addr* ad;

	if(!(ad = grab_slot(addrs, &naddrs, NADDRS, sizeof(*ad))))
		return;

	ad->ifi = ifi;
	ad->tag = tag;
	memcpy(ad->ip, ip, 4);
	ad->mask = mask;
}
