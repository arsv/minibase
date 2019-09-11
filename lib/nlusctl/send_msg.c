#include <sys/socket.h>

#include <nlusctl.h>

/* Client-side send with ancillary data. Just like uc_send_whole but
   calls sys_sendmsg() instead of sys_send(). */

int uc_send_msg(int fd, struct ucbuf* uc, void* ancillary, int alen)
{
	void* buf = uc->buf;
	void* ptr = uc->ptr;
	void* end = uc->end;
	int ret;

	if(ptr > end || ptr < buf)
		return -ENOBUFS;

	long len = ptr - buf;

	if(len < sizeof(struct ucmsg))
		return -EINVAL;

	struct iovec iov = {
		.base = buf,
		.len = len
	};
	struct msghdr packet = {
		.iov = &iov,
		.iovlen = 1,
		.control = ancillary,
		.controllen = alen
	};

	if((ret = sys_sendmsg(fd, &packet, 0)) < 0)
		;
	else if(ret < len)
		return -EINTR;
	else if(ret > len)
		return -EINVAL;

	return ret;
}
