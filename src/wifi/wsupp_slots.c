#include <sys/mman.h>

#include <string.h>
#include <format.h>
#include <printf.h>
#include <util.h>

#include "wsupp.h"

struct conn conns[NCONNS];
struct scan scans[NSCANS];
int nconns;
int nscans;

struct heap hp;

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
	if((sc = grab_slot(scans, &nscans, NSCANS, sizeof(*sc))))
		return sc;

	/* With no more empty slots left, try to sacrifice some weaker ones. */

	for(sc = scans; sc < scans + nscans; sc++) {
		if(sc->signal > -8000) /* -80dBm */
			continue;
		if(!(sc->flags & SF_STALE))
			continue;

		memzero(sc, sizeof(*sc));
		return sc;
	}

	return NULL;
}

void free_scan_slot(struct scan* sc)
{
	free_slot(scans, &nscans, sizeof(*sc), sc);
}

void clear_scan_table(void)
{
	nscans = 0;
	memzero(scans, sizeof(scans));

	hp.ptr = hp.org;
	maybe_trim_heap();
}

/* Heap structure:

       IE IE IE ... IE (status-message)

   IEs are from scan results. Each struct scan may hold a pointer to
   one of them. On every scan dump, the heap gets reset and all pointers
   re-written, see reset_ies_data(). IEs can easily take a page or two.

   The reply to CMD_WI_STATUS includes IEs (along with other stuff) and
   has to be assembled in the heap as well. It never remains there though.
   cmd_status() extends the heap, replies to the client and immediately
   shrinks the heap back. So it never really interferes with the IEs. */

void init_heap_ptrs(void)
{
	void* brk = sys_brk(NULL);

	hp.org = brk;
	hp.brk = brk;
	hp.ptr = brk;
}

int extend_heap(uint size)
{
	ulong left = hp.brk - hp.ptr;

	if(size <= left)
		return 0;

	ulong need = pagealign(size - left);

	void* old = hp.brk;
	void* brk = sys_brk(old + need);
	int ret;

	if((ret = brk_error(old, brk)) < 0)
		return ret;

	hp.brk = brk;

	return 0;
}

void* heap_store(void* buf, int len)
{
	void* stored;

	if(extend_heap(len) < 0)
		return NULL;

	stored = hp.ptr;
	hp.ptr += len;

	memcpy(stored, buf, len);

	return stored;
}

void maybe_trim_heap(void)
{
	long need = hp.ptr - hp.org;
	void* brk = hp.org + pagealign(need);

	if(brk >= hp.brk)
		return;

	sys_brk(brk);
	hp.brk = brk;
}
