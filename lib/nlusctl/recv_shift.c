#include <sys/socket.h>
#include <cdefs.h>
#include <string.h>
#include <nlusctl.h>

/* Client-side recv, with shift buffer to handle a continuous
   stream of messages. Meant to be used on blocking fd-s. */

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

int uc_recv_shift(int fd, struct urbuf* ur)
{
	int left, rd, ret;
	long total = 0;

	ur->msg = NULL;

	if((ret = take_complete_msg(ur)) >= 0)
		return 0;

	shift_buf(ur);

	while(1) {
		if((left = ur->end - ur->rptr) <= 0)
			return -ENOBUFS;

		if((rd = sys_recv(fd, ur->rptr, left, 0)) < 0)
			return rd;
		else if(!rd)
			break;

		ur->rptr += rd;
		total += rd;

		if((ret = take_complete_msg(ur)) >= 0)
			return total;
	}

	if(ur->mptr < ur->rptr)
		return -EBADMSG;
	else
		return -EBADF;
}
