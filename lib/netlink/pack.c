#include <sys/socket.h>
#include <sys/bind.h>
#include <sys/recv.h>
#include <sys/getpid.h>
#include <sys/sendto.h>

#include <string.h>

#include "base.h"
#include "ctx.h"
#include "pack.h"

static void* nl_alloc(struct netlink* nl, int size)
{
	if(nl->txover)
		return NULL;
	if(nl->txend + size > nl->txlen)
		return NULL;

	void* ptr = nl->txbuf + nl->txend;
	nl->txend += size;

	memset(ptr, 0, size);

	return ptr;
}

void* nl_start_packet(struct netlink* nl, int len)
{
	nl->txend = 0;
	nl->txover = 0;
	nl->seq++;

	return nl_alloc(nl, len);
}

void nl_new_cmd(struct netlink* nl, uint16_t fam, uint8_t cmd, uint8_t ver)
{
	struct nlgen* gen;

	nl_header(nl, gen, fam, 0,
			.cmd = cmd,
			.version = ver);
}

void nl_put(struct netlink* nl, uint16_t type, const void* buf, int len)
{
	int full = sizeof(struct nlattr) + len;
	struct nlattr* at = nl_alloc(nl, full);
	
	int ptr = nl->txend;
	int pad = (4 - (ptr % 4)) % 4;
	nl->txend += pad;

	if(!at) return;

	at->type = type;
	at->len = full;
	memcpy(at->payload, buf, len);

	((struct nlmsg*)(nl->txbuf))->len += full + pad;
}

void nl_put_str(struct netlink* nl, uint16_t type, const char* str)
{
	nl_put(nl, type, str, strlen(str) + 1);
}

void nl_put_u32(struct netlink* nl, uint16_t type, uint32_t val)
{
	nl_put(nl, type, &val, sizeof(val));
}

void nl_put_u64(struct netlink* nl, uint16_t type, uint64_t val)
{
	nl_put(nl, type, &val, sizeof(val));
}
