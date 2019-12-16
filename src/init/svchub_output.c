#include <sys/mman.h>
#include <sys/file.h>
#include <sys/iovec.h>

#include <string.h>

#include "common.h"
#include "svchub.h"

/* Well-behaved services are expected to be silent and/or use syslog
   for logging. However, if a service fails to start, or fails later
   at runtime for some reason, it can and should complain to stderr,
   hopefully indicating the reason it failed. In such cases supervisor
   stores the output and allows the user to review it later to diagnose
   the problem.

   Now generally error messages are small and the most important ones
   are issued last. However, we cannot rely on the whole output of the
   service being small, so the output gets stored in a fixed-size ring
   buffer. And because the service are not expected to output anything
   during normal operation, the buffers are allocated on demand. */

int flush_ring_buf(struct proc* rc)
{
	int ret;
	void* buf = rc->buf;
	int size = RINGSIZE;

	if(!buf)
		return -ENODATA;

	if((ret = sys_munmap(buf, size)) < 0) {
		rc->ptr = 0;
		return ret;
	} else {
		rc->buf = NULL;
		rc->ptr = 0;
	}

	return 0;
}

void check_proc(CTX, struct proc* rc)
{
	int ret, fd = rc->fd;
	void* buf = rc->buf;
	int ptr = rc->ptr;
	int size = RINGSIZE;

	if(!buf) {
		int proto = PROT_READ | PROT_WRITE;
		int flags = MAP_PRIVATE | MAP_ANONYMOUS;

		buf = sys_mmap(NULL, size, proto, flags, -1, 0);

		if(mmap_error(buf))
			goto close;

		ptr = 0;
		rc->buf = buf;
		rc->ptr = ptr;
	}

	struct iovec iov[2];
	int off = ptr % size;
	iov[0].base = buf + off;
	iov[0].len = size - off;
	iov[1].base = buf;
	iov[1].len = off;
	int iovcnt = off ? 2 : 1;

	if((ret = sys_readv(fd, iov, iovcnt)) < 0)
		goto close;

	ptr += ret;

	if(ptr > RINGSIZE)
		ptr = RINGSIZE + (ptr % RINGSIZE);

	rc->ptr = ptr;

	return;
close:
	close_proc(ctx, rc);
}

void close_proc(CTX, struct proc* rc)
{
	int fd = rc->fd;

	if(fd < 0) return;

	sys_close(fd);

	rc->fd = -1;

	ctx->pollset = 0;
}
