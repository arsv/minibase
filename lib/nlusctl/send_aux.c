#include <sys/socket.h>

#include <nlusctl.h>

int uc_send_aux(int fd, struct ucbuf* uc, struct ucaux* ux)
{
	int ret;
	struct iovec iov;

	if((ret = uc_iov_hdr(&iov, uc)) < 0)
		return ret;

	if(ux->ptr < 0)
		return -ENOBUFS;

	struct msghdr hdr = {
		.name = NULL,
		.namelen = 0,
		.iov = &iov,
		.iovlen = 1,
		.control = ux->buf,
		.controllen = ux->ptr,
		.flags = 0
	};

	return sys_sendmsg(fd, &hdr, MSG_NOSIGNAL);
}
