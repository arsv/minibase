#include <sys/ppoll.h>
#include <sys/socket.h>

#include <nlusctl.h>

/* Server-side send(). Tries to push the whole message, times out
   if it takes too long. We assume the client should be expecting
   the message, and recv()ing it, if that's not the case the server
   should drop the connection.

   Note poll is necessary here even if the message is short.
   Relative timing between send-recv in the client, and recv-send in
   the server is uncertain, so by the time server calls send() the
   client may not be recv()ing yet. */

int uc_send_timed(int fd, struct ucbuf* uc)
{
	void* buf = uc->buf;
	void* ptr = uc->ptr;
	void* end = uc->end;

	if(ptr > end || ptr < buf)
		return -ENOBUFS;

	long len = uc->ptr - uc->buf;

	if(len < sizeof(struct ucmsg))
		return -EINVAL;

	struct timespec ts = { 1, 0 };
	struct pollfd pfd = { fd, POLLOUT, 0 };
	int ret;
again:
	if((ret = sys_send(fd, buf, len, 0)) >= len)
		return ret;

	if(ret < 0) {
		if(ret != -EAGAIN)
			return ret;
	} else if(ret) {
		buf += ret;
		len -= ret;
	}

	if((ret = sys_ppoll(&pfd, 1, &ts, NULL)) < 0)
		return ret;
	else if(ret == 0)
		return -ETIMEDOUT;

	goto again;
}
