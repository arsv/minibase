#include <sys/socket.h>

#include <netlink.h>
#include <netlink/pack.h>

int nc_send(int fd, struct ncbuf* nc)
{
	void* buf = nc->brk;
	void* ptr = nc->ptr;
	void* end = nc->end;

	if(ptr > end || ptr < buf)
		return -ENOBUFS;

	long len = ptr - buf;
	struct nlmsg* msg = buf;
	void* mhe = buf + sizeof(*msg); /* message header end */

	if(mhe > ptr || mhe < buf)
		return -EINVAL;

	msg->len = ptr - buf;

	return sys_send(fd, buf, len, 0);
}
