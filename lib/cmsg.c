#include <cmsg.h>
#include <string.h>

ulong cmsg_align(ulong len)
{
	int mask = sizeof(long) - 1;
	return ((len + mask) & ~mask);
}

struct cmsg* cmsg_first(void* p, void* e)
{
	struct cmsg* cm = p;
	ulong len = e - p;

	if(len < sizeof(cm))
		return NULL;
	if(len < cmsg_align(cm->len))
		return NULL;

	return cm;
}

struct cmsg* cmsg_next(void* p, void* e)
{
	struct cmsg* cm;

	if(!(cm = cmsg_first(p, e)))
		return NULL;

	p += cmsg_align(cm->len);

	return cmsg_first(p, e);
}

void* cmsg_put(void* p, void* e, int lvl, int type, void* data, ulong dlen)
{
	if(!p) return p;

	int msglen = sizeof(struct cmsg) + dlen;
	long need = cmsg_align(msglen);
	long have = e - p;

	if(have < need)
		return NULL;

	struct cmsg* cm = p;
	cm->len = msglen;
	cm->level = lvl;
	cm->type = type;
	memcpy(cm->data, data, dlen);

	return p + need;
}

int cmsg_paylen(struct cmsg* cm)
{
	return cm->len - sizeof(*cm);
}

void* cmsg_payload(struct cmsg* cm)
{
	return cm->data;
}

void* cmsg_paylend(struct cmsg* cm)
{
	return cm->data + (cm->len - sizeof(*cm));
}

struct cmsg* cmsg_get(void* p, void* e, int lvl, int type)
{
	struct cmsg* cm;

	for(cm = cmsg_first(p, e); cm; cm = cmsg_next(cm, e))
		if(cm->level == lvl && cm->type == type)
			return cm;

	return NULL;
}
