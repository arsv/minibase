#include <sys/socket.h>

#include <nlusctl.h>

int uc_send_iov(int fd, struct iovec* iov, int iovcnt)
{
	struct msghdr hdr = {
		.name = NULL,
		.namelen = 0,
		.iov = iov,
		.iovlen = iovcnt,
		.control = NULL,
		.controllen = 0,
		.flags = 0
	};

	return sys_sendmsg(fd, &hdr, MSG_NOSIGNAL);
}
