#include <netlink.h>
#include <netlink/recv.h>

void nr_reset(struct nrbuf* nr)
{
	void* buf = nr->buf;

	nr->msg = buf;
	nr->ptr = buf;
}
