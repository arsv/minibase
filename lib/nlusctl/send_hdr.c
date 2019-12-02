#include <sys/socket.h>
#include <nlusctl.h>

int uc_iov_hdr(struct iovec* iov, struct ucbuf* uc)
{
	void* buf = uc->buf;
	int cap = uc->len;
	struct ucattr* msg = buf;

	if(cap < sizeof(*msg))
		goto err;

	int len = msg->len;

	if(len < sizeof(*msg))
		goto err;

	iov->base = buf + 2;
	iov->len = len - 2;

	return 0;
err:
	return -ENOBUFS;
}
