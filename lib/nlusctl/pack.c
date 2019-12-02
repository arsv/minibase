#include <nlusctl.h>

void uc_buf_set(struct ucbuf* uc, void* buf, int len)
{
	uc->buf = buf;
	uc->len = len;
}

void uc_put_hdr(struct ucbuf* uc, int cmd)
{
	struct ucattr* msg = uc->buf;

	msg->key = cmd;
	msg->len = sizeof(*msg);
}
