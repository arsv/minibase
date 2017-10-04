#include <cdefs.h>
#include <string.h>

#include "base.h"
#include "ctx.h"
#include "pack.h"

void* nl_alloc(struct netlink* nl, int size)
{
	if(nl->txover)
		return NULL;
	if(nl->txend + size > nl->txlen)
		return NULL;

	int pad = (4 - (size % 4)) % 4;
	void* ptr = nl->txbuf + nl->txend;
	nl->txend += size + pad;

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
	struct nlmsg* msg;
	struct nlattr* at;
	
	if(!(at = nl_alloc(nl, full)))
		return;

	at->type = type;
	at->len = full;
	memcpy(at->payload, buf, len);

	if(!(msg = nl_tx_msg(nl)))
		return;

	msg->len = nl->txend;
}

void nl_put_empty(struct netlink* nl, uint16_t type)
{
	nl_put(nl, type, NULL, 0);
}

void nl_put_u8(struct netlink* nl, uint16_t type, uint8_t val)
{
	nl_put(nl, type, &val, sizeof(val));
}

void nl_put_u32(struct netlink* nl, uint16_t type, uint32_t val)
{
	nl_put(nl, type, &val, sizeof(val));
}

void nl_put_u64(struct netlink* nl, uint16_t type, uint64_t val)
{
	nl_put(nl, type, &val, sizeof(val));
}
