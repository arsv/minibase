#include <sys/socket.h>
#include <sys/bind.h>
#include <sys/recv.h>
#include <sys/getpid.h>
#include <sys/sendto.h>

#include <string.h>
#include <netlink.h>

static void* nl_alloc(struct netlink* nl, int size)
{
	if(nl->txover)
		return NULL;
	if(nl->txend + size > nl->txlen)
		return NULL;

	void* ptr = nl->txbuf + nl->txend;
	nl->txend += size;

	return ptr;
}

void* nl_new(struct netlink* nl, int len)
{
	nl->txend = 0;
	nl->txover = 0;
	nl->seq++;

	return nl_alloc(nl, len);
}

void nl_new_msg(struct netlink* nl, int to)
{
	struct nlmsg* msg = nl_new(nl, sizeof(struct nlmsg));

	if(!msg) return;

	msg->type = to;
	msg->flags = 0;
	msg->pid = 0;
	msg->seq = nl->seq;
}

void nl_new_cmd(struct netlink* nl, int to, uint8_t cmd, uint8_t version)
{
	struct nlgen* msg = nl_new(nl, sizeof(struct nlgen));

	if(!msg) return;

	msg->type = to;
	msg->flags = 0;
	msg->pid = 0;
	msg->seq = nl->seq;
	msg->cmd = cmd;
	msg->version = version;
	msg->unused = 0;
}

void nl_put_astr(struct netlink* nl, uint16_t type, const char* str)
{
	int slen = strlen(str) + 1;
	int pad = (4 - (slen & 3)) & 3;
	int alen = sizeof(struct nlattr) + slen;
	struct nlattr* attr = nl_alloc(nl, alen + pad);

	if(!attr) return;

	attr->len = alen;
	attr->type = type;

	memcpy(attr->payload, str, slen);

	if(!pad) return;
	
	memset(attr->payload + slen, 0, pad);
}

void nl_put_u32(struct netlink* nl, uint16_t type, uint32_t val)
{
	int size = sizeof(struct nlattr) + sizeof(uint32_t);
	struct nlattr* attr = nl_alloc(nl, size);

	if(!attr) return;

	attr->type = type;
	attr->len = size;
	*((uint32_t*) attr->payload) = val;
}

void nl_put_u64(struct netlink* nl, uint16_t type, uint64_t val)
{
	int size = sizeof(struct nlattr) + sizeof(uint64_t);
	struct nlattr* attr = nl_alloc(nl, size);

	if(!attr) return;

	attr->type = type;
	attr->len = size;
	*((uint64_t*) attr->payload) = val;
}
