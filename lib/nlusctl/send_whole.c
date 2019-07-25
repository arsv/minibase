#include <sys/socket.h>

#include <nlusctl.h>

/* Client-side send. Client messages (commands) are invariably small,
   never chains, so send() should alway be atomic here.

   Meant to be used on blocking connections. */

int uc_send_whole(int fd, struct ucbuf* uc)
{
	void* buf = uc->brk;
	long len = uc->ptr - uc->brk;
	int ret;

	if(len < sizeof(struct ucmsg))
		return -EINVAL;
	if(uc->over)
		return -ENOBUFS;

	if((ret = sys_send(fd, buf, len, 0)) < 0)
		;
	else if(ret < len)
		return -EINTR;
	else if(ret > len)
		return -EINVAL;

	return ret;
}
