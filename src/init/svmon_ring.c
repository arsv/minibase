#include <sys/mmap.h>
#include <sys/file.h>
#include <printf.h>
#include <string.h>
#include <null.h>

#include "config.h"
#include "svmon.h"

static char* ringarea;
static int ringlength;

static int extend_ring_area(void)
{
	int prot = PROT_READ | PROT_WRITE;
	int mflags = MAP_PRIVATE | MAP_ANONYMOUS;
	int rflags = MREMAP_MAYMOVE;

	int size = ringlength + PAGE;
	long new;

	if(ringarea) {
		new = sys_mremap(ringarea, ringlength, size, rflags);
		tracef("remap %i -> %i = %lX\n", ringlength, size, new);
	} else {
		new = sys_mmap(NULL, size, prot, mflags, -1, 0);
		tracef("mmap %i = %lX\n", size, new);
	}

	if(mmap_error(new)) {
		report("mmap failed", NULL, new);
		return new;
	}

	ringarea = (char*)new;
	ringlength = size;

	return 0;
}

static void index_ring_usage(short* use, int count)
{
	struct proc* rc;

	memzero(use, count);

	for(rc = firstrec(); rc; rc = nextrec(rc)) {
		if(!rc->ptr)
			continue;
		if(rc->idx < 0 || rc->idx >= count)
			continue;

		use[rc->idx] = 1 + (rc - procs);
	}

}

char* ring_buf_for(struct proc* rc)
{
	int len = RINGSIZE;
	int off = len * rc->idx;
	int end = off + len;

	if(end > ringlength)
		return NULL;

	return ringarea + off;
}

char* map_ring_buf(struct proc* rc)
{
	int i, count = ringlength / RINGSIZE;
	short idx[count];

	index_ring_usage(idx, count);

	for(i = 0; i < count; i++)
		if(!idx[i])
			break;
	if(i < count)
		tracef("using hole\n");
	else if(extend_ring_area()) {
		tracef("extend failed\n");
		return NULL;
	}

	rc->idx = i;
	rc->ptr = 0;

	tracef("proc %s ring idx %i\n", rc->name, i);

	return ring_buf_for(rc);
}

int pages(int len)
{
	len += (PAGE - len % PAGE) % PAGE;
	return len / PAGE;
}

void trim_ring_area(void)
{
	int count = ringlength / RINGSIZE;
	short idx[count];

	index_ring_usage(idx, count);

	while(count > 0 && !idx[count-1])
		count--;

	int need = count * RINGSIZE;
	int havepages = pages(ringlength);
	int needpages = pages(need);

	if(needpages >= havepages)
		return;

	if(needpages > 0) {
		int size = needpages * PAGE;
		long new = sys_mremap(ringarea, ringlength, size, 0);

		if(mmap_error(new))
			return;

		ringarea = (char*)new;
		ringlength = size;
	} else {
		if(sys_munmap(ringarea, ringlength) < 0)
			return;

		ringarea = NULL;
		ringlength = 0;
	}
}

void flush_ring_buf(struct proc* rc)
{
	tracef("flush for %s (was %i)\n", rc->name, rc->idx);
	rc->idx = -1;
	rc->ptr = 0;
}

static int wrapto(int x, int size)
{
	if(x > size)
		return size + x % size;
	else
		return x;
}

int read_into_ring_buf(struct proc* rc, int fd)
{
	int ptr = rc->ptr;
	char* buf;
	int ret;

	if(ptr)
		buf = ring_buf_for(rc);
	else
		buf = map_ring_buf(rc);
	if(!buf)
		return -ENOMEM;

	int off = ptr % RINGSIZE;
	char* start = buf + off;
	int avail = RINGSIZE - off;

	if((ret = sys_read(fd, start, avail)) <= 0)
		return ret;

	rc->ptr = wrapto(ptr + ret, RINGSIZE);

	return ret;
}
