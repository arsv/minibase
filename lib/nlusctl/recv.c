#include <bits/errno.h>
#include <sys/sockio.h>
#include <string.h>

#include "../nlusctl.h"

static void shift_buf(struct urbuf* ur)
{
	int shift = ur->mptr - ur->buf;

	ur->msg = NULL;

	if(!shift) return;

	memmove(ur->buf, ur->mptr, ur->rptr - ur->mptr);

	ur->mptr -= shift;
	ur->rptr -= shift;
}

static int take_complete_msg(struct urbuf* ur)
{
	struct ucmsg* msg = NULL;

	if(!(msg = uc_msg(ur->mptr, ur->rptr - ur->mptr)))
		return -EBADMSG;

	ur->msg = msg;
	ur->mptr += msg->len;

	return msg->len;
}

int uc_recv(int fd, struct urbuf* ur, int block)
{
	int left, rd, ret;
	int flags = block ? 0 : MSG_DONTWAIT;
	long total = 0;

	ur->msg = NULL;

	if((ret = take_complete_msg(ur)) >= 0)
		return 0;

	shift_buf(ur);

	while(1) {
		if((left = ur->end - ur->rptr) <= 0)
			return -ENOBUFS;

		if((rd = sys_recv(fd, ur->rptr, left, flags)) < 0)
			return rd;
		else if(!rd)
			break;

		ur->rptr += rd;
		total += rd;

		if((ret = take_complete_msg(ur)) >= 0)
			return total;

		flags = 0;
	}

	if(ur->mptr < ur->rptr)
		return -EBADMSG;
	else
		return -EBADF;
}
