#include <bits/socket.h>
#include <nlusctl.h>
#include <cmsg.h>

void ux_putf1(struct ucaux* ux, int fd)
{
	void* buf = ux->buf;
	struct cmsg* cm = buf;
	int len = sizeof(*cm) + sizeof(fd);
	int mask = sizeof(long) - 1;
	int size = (len + mask) & ~mask;

	cm->len = len;
	cm->level = SOL_SOCKET;
	cm->type = SCM_RIGHTS;
	*((int*)cm->data) = fd;

	ux->ptr = size;
}
