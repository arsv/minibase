#include <sys/socket.h>
#include <sys/bind.h>
#include <sys/recv.h>
#include <sys/getpid.h>
#include <sys/sendto.h>

#include <string.h>
#include <netlink.h>

int nl_msg_len(struct nlmsg* msg) { return msg->len - sizeof(*msg); }
int nl_gen_len(struct nlgen* msg) { return msg->len - sizeof(*msg); }
int nl_err_len(struct nlerr* msg) { return msg->len - sizeof(*msg); }
int nl_attr_len(struct nlattr* at) { return at->len - sizeof(*at); }

static int check_zstr(char* buf, int len)
{
	int i;

	for(i = 0; i < len; i++)
		if(!buf[i])
			break;
	if(i >= len)
		return 0; /* not 0-terminated */
	if(i < len - 1)
		return 0; /* trailing garbage */

	return 1;
}

static int extend_to_4bytes(int n)
{
	return n + ((4 - (n & 3)) & 3);
}

static int check_nest(char* buf, int len)
{
	char* end = buf + len;

	if(len <= sizeof(struct nlattr))
		return 0;

	char* p = buf;
	while(p < end) {
		struct nlattr* sub = (struct nlattr*) p;
		uint16_t skip = extend_to_4bytes(sub->len);

		if(!skip) break;

		p += skip;
	}

	return (p == end);
}

struct nlattr* attr_0_in(char* buf, int len)
{
	if(len < sizeof(struct nlattr))
		return NULL;

	struct nlattr* at = (struct nlattr*)buf;

	if(len < at->len)
		return NULL;

	return at;
}

static int ptr_in_buf(char* buf, int len, char* ptr)
{
	if(!ptr)
		return 0;
	if(ptr < buf)
		return 0;
	else if(ptr >= buf + len)
		return 0;
	return 1;
}

struct nlattr* attr_n_in(char* buf, int len, struct nlattr* curr)
{
	char* pcurr = (char*)curr;

	if(!ptr_in_buf(buf, len, pcurr)) return NULL;

	char* pnext = pcurr + extend_to_4bytes(curr->len);

	if(!ptr_in_buf(buf, len, pnext)) return NULL;

	return (struct nlattr*)pnext;	
}

struct nlattr* nl_get_0(struct nlgen* msg)
{
	return attr_0_in(msg->payload, nl_gen_len(msg));
}

struct nlattr* nl_get_n(struct nlgen* msg, struct nlattr* curr)
{
	return attr_n_in(msg->payload, nl_gen_len(msg), curr);
}

struct nlattr* nl_sub_0(struct nlattr* at)
{
	return attr_0_in(at->payload, nl_attr_len(at));
}

struct nlattr* nl_sub_n(struct nlattr* at, struct nlattr* curr)
{
	return attr_n_in(at->payload, nl_attr_len(at), curr);
}

typedef int (*atck)(struct nlattr* at, int arg);

struct nlattr* find_in(char* buf, int len, int i, atck ac, int arg)
{
	struct nlattr* at;

	for(at = attr_0_in(buf, len); at; at = attr_n_in(buf, len, at))
		if(at->type != i)
			continue;
		else if(ac(at, arg))
			return at;
		else
			break;

	return NULL;
}

static int atck_any(struct nlattr* at, int _)
{
        return 1;
}

static int atck_int(struct nlattr* at, int size)
{
	return (nl_attr_len(at) == size);
}

static int atck_str(struct nlattr* at, int _)
{
	return check_zstr(at->payload, nl_attr_len(at));
}

static int atck_nest(struct nlattr* at, int _)
{
	return check_nest(at->payload, nl_attr_len(at));
}

static void* payload(struct nlattr* at)
{
	return at ? at->payload : NULL;
}

static struct nlattr* from_msg(struct nlgen* msg, int i, atck ac, int arg)
{
	return find_in(msg->payload, nl_gen_len(msg), i, ac, arg);
}

static struct nlattr* from_attr(struct nlattr* at, int i, atck ac, int arg)
{
	return find_in(at->payload, nl_attr_len(at), i, ac, arg);
}

uint16_t* nl_get_u16(struct nlgen* msg, int i)
{
	return payload(from_msg(msg, i, atck_int, sizeof(uint16_t)));
}

uint32_t* nl_get_u32(struct nlgen* msg, int i)
{
	return payload(from_msg(msg, i, atck_int, sizeof(uint32_t)));
}

uint64_t* nl_get_u64(struct nlgen* msg, int i)
{
	return payload(from_msg(msg, i, atck_int, sizeof(uint64_t)));
}

char* nl_get_str(struct nlgen* msg, int i)
{
	return payload(from_msg(msg, i, atck_str, 0));
}

struct nlattr* nl_get_nest(struct nlgen* msg, int i)
{
	return from_msg(msg, i, atck_nest, 0);
}

struct nlattr* nl_sub(struct nlattr* at, int i)
{
	return from_attr(at, i, atck_any, 0);
}

uint16_t* nl_sub_u16(struct nlattr* at, int i)
{
	return payload(from_attr(at, i, atck_int, sizeof(uint16_t)));
}

uint32_t* nl_sub_u32(struct nlattr* at, int i)
{
	return payload(from_attr(at, i, atck_int, sizeof(uint32_t)));
}

int32_t* nl_sub_i32(struct nlattr* at, int i)
{
	return (int32_t*) nl_sub_u32(at, i);
}

uint64_t* nl_sub_u64(struct nlattr* at, int i)
{
	return payload(from_attr(at, i, atck_int, sizeof(uint64_t)));
}

char* nl_sub_str(struct nlattr* at, int i)
{
	return payload(from_attr(at, i, atck_str, 0));
}

struct nlattr* nl_sub_nest(struct nlattr* at, int i)
{
	return from_attr(at, i, atck_nest, 0);
}

void* nl_sub_len(struct nlattr* at, int i, int len)
{
	return payload(from_attr(at, i, atck_int, len));
}

struct nlerr* nl_err(struct nlmsg* msg)
{
	if(msg->type != NLMSG_ERROR)
		return NULL;
	if(msg->len < sizeof(struct nlerr))
		return NULL;

	return (struct nlerr*) msg;
}

struct nlgen* nl_gen(struct nlmsg* msg)
{
	if(msg->len < sizeof(struct nlgen))
		return NULL;

	struct nlgen* gen = (struct nlgen*) msg;

	if(gen->len > sizeof(struct nlgen))
		if(!check_nest(gen->payload, nl_gen_len(gen)))
			return NULL;

	return gen;
}

int nl_is_nest(struct nlattr* at)
{
	return check_nest(at->payload, nl_attr_len(at));
}

int nl_is_str(struct nlattr* at)
{
	return check_zstr(at->payload, nl_attr_len(at));
}

uint16_t* nl_u16(struct nlattr* at)
{
	return atck_int(at, sizeof(uint16_t)) ? (uint16_t*)at->payload : NULL;
}

uint32_t* nl_u32(struct nlattr* at)
{
	return atck_int(at, sizeof(uint32_t)) ? (uint32_t*)at->payload : NULL;
}

uint64_t* nl_u64(struct nlattr* at)
{
	return atck_int(at, sizeof(uint64_t)) ? (uint64_t*)at->payload : NULL;
}

char* nl_str(struct nlattr* at)
{
	return atck_str(at, 0) ? at->payload : NULL;
}
