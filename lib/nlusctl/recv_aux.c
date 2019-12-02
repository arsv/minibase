#include <sys/socket.h>
#include <nlusctl.h>

int uc_recv_aux(int fd, void* buf, int len, struct ucaux* ux)
{
	int ret;

	struct iovec iov = {
		.base = buf + 2,
		.len = len - 2
	};
	struct msghdr hdr = {
		.name = NULL,
		.namelen = 0,
		.iov = &iov,
		.iovlen = 1,
		.control = ux->buf,
		.controllen = sizeof(ux->buf),
		.flags = 0
	};

	if((ret = sys_recvmsg(fd, &hdr, 0)) < 0)
		return ret;

	ux->ptr = hdr.controllen;

	return ret;
}
