#include <null.h>
#include "../nlusctl.h"

static struct ucmsg* uc_msg_hdr(char* buf, int len)
{
	struct ucmsg* msg = (struct ucmsg*) buf;

	if(len < sizeof(*msg))
		return NULL;

	return msg;
}

int uc_msglen(char* buf, int len)
{
	struct ucmsg* msg;

	if(!(msg = uc_msg_hdr(buf, len)))
		return 0;

	return msg->len;
}

struct ucmsg* uc_msg(char* buf, int len)
{
	struct ucmsg* msg;

	if(!(msg = uc_msg_hdr(buf, len)))
		return NULL;
	if(msg->len > len)
		return NULL;

	return msg;
}

static struct ucattr* uc_get_0_in(char* buf, int len)
{
	struct ucattr* at = (struct ucattr*) buf;

	if(len < sizeof(*at))
		return NULL;
	if(at->len > len)
		return NULL;

	return at;
}

static struct ucattr* uc_get_n_in(char* buf, int len, struct ucattr* at)
{
	char* end = buf + len;
	char* ptr = (char*) at;

	if(ptr < buf)
		return NULL;
	if(ptr > end)
		return NULL;
	if(!at->len)
		return NULL;

	int aln = at->len;
	aln += (4 - aln % 4) % 4;
	ptr += aln;

	if(ptr + sizeof(*at) > end)
		return NULL;

	at = (struct ucattr*) ptr;

	if(ptr + at->len > end)
		return NULL;

	return at;
}

struct ucattr* uc_get_0(struct ucmsg* msg)
{
	return uc_get_0_in(msg->payload, msg->len - sizeof(*msg));
}

struct ucattr* uc_get_n(struct ucmsg* msg, struct ucattr* ab)
{
	return uc_get_n_in(msg->payload, msg->len - sizeof(*msg), ab);
}

struct ucattr* uc_sub_0(struct ucattr* ab)
{
	return uc_get_0_in(ab->payload, ab->len - sizeof(*ab));
}

struct ucattr* uc_sub_n(struct ucattr* ab, struct ucattr* at)
{
	return uc_get_n_in(ab->payload, ab->len - sizeof(*ab), at);
}

struct ucattr* uc_get(struct ucmsg* msg, int key)
{
	struct ucattr* at;

	for(at = uc_get_0(msg); at; at = uc_get_n(msg, at))
		if(at->key == key)
			return at;

	return NULL;
}

struct ucattr* uc_sub(struct ucattr* bt, int key)
{
	struct ucattr* at;

	for(at = uc_sub_0(bt); at; at = uc_sub_n(bt, at))
		if(at->key == key)
			return at;

	return NULL;
}

static int is_zstr(char* buf, int len)
{
	char* p;

	for(p = buf; p < buf + len - 1; p++)
		if(!*p) return 0;

	return *p ? 0 : 1;
}
void* uc_payload(struct ucattr* at)
{
	return (void*)(at->payload);
}

int uc_paylen(struct ucattr* at)
{
	return at->len - sizeof(*at);
}

void* uc_is_bin(struct ucattr* at, int key, int len)
{
	if(!at || at->key != key)
		return NULL;
	if(uc_paylen(at) != len)
		return NULL;

	return uc_payload(at);
}

char* uc_is_str(struct ucattr* at, int key)
{
	if(!at || at->key != key)
		return NULL;
	if(!is_zstr(uc_payload(at), uc_paylen(at)))
		return NULL;

	return uc_payload(at);
}

int* uc_is_int(struct ucattr* at, int key)
{
	return uc_is_bin(at, key, 4);
}

void* uc_get_bin(struct ucmsg* msg, int key, int len)
{
	return uc_is_bin(uc_get(msg, key), key, len);
}

int* uc_get_int(struct ucmsg* msg, int key)
{
	return uc_is_int(uc_get(msg, key), key);
}

char* uc_get_str(struct ucmsg* msg, int key)
{
	return uc_is_str(uc_get(msg, key), key);
}

void* uc_sub_bin(struct ucattr* at, int key, int len)
{
	return uc_is_bin(uc_sub(at, key), key, len);
}

int* uc_sub_int(struct ucattr* at, int key)
{
	return uc_is_int(uc_sub(at, key), key);
}

char* uc_sub_str(struct ucattr* at, int key)
{
	return uc_is_str(uc_sub(at, key), key);
}
