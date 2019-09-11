#include <sys/socket.h>

#include <nlusctl.h>

/* Client-side send. Client messages (commands) are invariably small,
   so send() should alway be atomic here.

   Meant to be used on blocking connections. */

int uc_send_whole(int fd, struct ucbuf* uc)
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

	if((ret = sys_send(fd, buf, len, 0)) < 0)
		;
	else if(ret < len)
		return -EINTR;
	else if(ret > len)
		return -EINVAL;

	return ret;
}
