#include <sys/socket.h>
#include <nlusctl.h>

/* Server-side recv for packet with ancillary data.

   Same code as uc_recv_whole basically, with one additional
   buffer for the ancillary data. And sys_recvmsg() instead
   of sys_recv(). */

int uc_recvmsg(int fd, void* buf, int len, struct ucaux* ux)
{
	int ret, flags = MSG_CMSG_CLOEXEC;
	struct ucmsg* msg = buf;

	struct iovec iov = {
		.base = buf,
		.len = len
	};
	struct msghdr packet = {
		.iov = &iov,
		.iovlen = 1,
		.control = ux->buf,
		.controllen = ux->len
	};

	ux->len = 0;

	if((ret = sys_recvmsg(fd, &packet, flags)) < 0)
		return ret;
	if((ret < sizeof(*msg))) /* should never happen */
		return -EBADMSG;
	if(msg->len != ret)
		return -EBADMSG;

	ux->len = packet.controllen;

	return ret;
}
