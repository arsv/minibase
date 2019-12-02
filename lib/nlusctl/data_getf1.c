#include <bits/socket.h>
#include <nlusctl.h>
#include <cmsg.h>

int ux_getf1(struct ucaux* ux)
{
	void* buf = ux->buf;
	struct cmsg* cm = buf;
	int* fp = (void*)cm->data;
	int len = sizeof(*cm) + sizeof(*fp);
	int mask = sizeof(long) - 1;
	int size = (len + mask) & ~mask;
	int ptr = ux->ptr;

	if(ptr < len || ptr > size)
		goto bad;
	if(cm->len != len)
		goto bad;
	if(cm->level != SOL_SOCKET)
		goto bad;
	if(cm->type != SCM_RIGHTS)
		goto bad;

	int fd = *fp;

	*fp = -1;

	return fd;
bad:
	return -1;
}
