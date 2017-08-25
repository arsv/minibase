#include <bits/errno.h>
#include <bits/null.h>
#include <sys/socket.h>
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

int uc_recvmsg(int fd, struct urbuf* ur, struct ucbuf* uc, int block)
{
	int left, rd, ret;
	int flags = MSG_CMSG_CLOEXEC;
	long total = 0;

	if(!block) flags |= MSG_DONTWAIT;

	uc->ptr = uc->brk;
	ur->msg = NULL;

	if((ret = take_complete_msg(ur)) >= 0)
		return 0;

	shift_buf(ur);

	struct iovec iov;
	struct msghdr msg = {
		.iov = &iov,
		.iovlen = 1,
		.control = uc->brk,
		.controllen = uc->end - uc->brk
	};

	while(1) {
		if((left = ur->end - ur->rptr) <= 0)
			return -ENOBUFS;

		iov.base = ur->rptr;
		iov.len = left;

		if((rd = sys_recvmsg(fd, &msg, flags)) < 0)
			return rd;
		else if(!rd)
			break;

		ur->rptr += rd;
		total += rd;
		uc->ptr = uc->brk + msg.controllen;

		if((ret = take_complete_msg(ur)) >= 0)
			return total;

		flags = 0;
	}

	if(ur->mptr < ur->rptr)
		return -EBADMSG;
	else
		return -EBADF;
}
