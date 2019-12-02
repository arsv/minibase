#include <sys/socket.h>
#include <nlusctl.h>

int uc_recv(int fd, void* buf, int len)
{
	struct iovec iov = {
		.base = buf + 2,
		.len = len - 2
	};
	struct msghdr hdr = {
		.name = NULL,
		.namelen = 0,
		.iov = &iov,
		.iovlen = 1,
		.control = NULL,
		.controllen = 0,
		.flags = 0
	};

	return sys_recvmsg(fd, &hdr, 0);
}
