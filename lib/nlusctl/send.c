#include <sys/socket.h>
#include <nlusctl.h>

int uc_send(int fd, struct ucbuf* uc)
{
	int ret;
	struct iovec iov;

	if((ret = uc_iov_hdr(&iov, uc)) < 0)
		return ret;

	struct msghdr hdr = {
		.name = NULL,
		.namelen = 0,
		.iov = &iov,
		.iovlen = 1,
		.control = NULL,
		.controllen = 0,
		.flags = 0
	};

	return sys_sendmsg(fd, &hdr, MSG_NOSIGNAL);
}
