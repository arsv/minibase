#include <sys/socket.h>
#include "base.h"
#include "cmd.h"

int nc_send(int fd, struct ncbuf* nc)
{
	void* buf = nc->brk;
	void* ptr = nc->ptr;

	if(!buf) return -ENOBUFS;

	long len = ptr - buf;

	return sys_send(fd, buf, len, 0);
}
