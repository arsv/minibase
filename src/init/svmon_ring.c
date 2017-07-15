#include <sys/mmap.h>
#include <sys/file.h>
#include <null.h>

#include "config.h"
#include "svmon.h"

static int mmap_ring_buf(struct proc* rc)
{
	int prot = PROT_READ | PROT_WRITE;
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;
	long ret = sys_mmap(NULL, RINGSIZE, prot, flags, -1, 0);

	if(mmap_error(ret))
		return ret;

	rc->buf = (char*)ret;
	rc->ptr = 0;

	return 0;
}

void flush_ring_buf(struct proc* rc)
{
	sys_munmap(rc->buf, RINGSIZE);

	rc->buf = NULL;
	rc->ptr = 0;
}

int read_into_ring_buf(struct proc* rc, int fd)
{
	int ret;

	if(rc->buf)
		;
	else if((ret = mmap_ring_buf(rc)) < 0)
		return ret;

	int off = rc->ptr % RINGSIZE;

	char* start = rc->buf + off;
	int avail = RINGSIZE - off;

	if((ret = sys_read(fd, start, avail)) <= 0)
		return ret;

	int ptr = rc->ptr + ret;

	if(ptr >= RINGSIZE)
		ptr = RINGSIZE + ptr % RINGSIZE;

	rc->ptr = ptr;

	return ret;
}
