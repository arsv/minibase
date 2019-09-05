#include <sys/socket.h>
#include "base.h"
#include "cmd.h"

int nc_recv_ack(int fd, void* buf, int len, int seq)
{
	int rd;
	struct nlmsg* msg;
	struct nlerr* err;

	if((rd = sys_recv(fd, buf, len, 0)) < 0)
		return rd;
	if(!(msg = nl_msg(buf, rd)))
		return -EBADMSG;
	if(rd != nl_len(msg))
		return -EBADMSG;
	if(!(err = nl_err(msg)))
		return -EBADMSG;
	if(err->seq != seq)
		return -EILSEQ;

	return err->errno;
}
