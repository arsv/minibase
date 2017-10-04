#include <cdefs.h>
#include <nlusctl.h>

static int is_zstr(char* buf, int len)
{
	char* p;

	for(p = buf; p < buf + len - 1; p++)
		if(!*p) return 0;

	return *p ? 0 : 1;
}

char* uc_is_str(struct ucattr* at, int key)
{
	if(!at || at->key != key)
		return NULL;
	if(!is_zstr(uc_payload(at), uc_paylen(at)))
		return NULL;

	return uc_payload(at);
}

void* uc_is_bin(struct ucattr* at, int key, int len)
{
	if(!at || at->key != key)
		return NULL;
	if(uc_paylen(at) != len)
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

void* uc_sub_bin(struct ucattr* at, int key, int len)
{
	return uc_is_bin(uc_sub(at, key), key, len);
}

int* uc_sub_int(struct ucattr* at, int key)
{
	return uc_is_int(uc_sub(at, key), key);
}

char* uc_get_str(struct ucmsg* msg, int key)
{
	return uc_is_str(uc_get(msg, key), key);
}

char* uc_sub_str(struct ucattr* at, int key)
{
	return uc_is_str(uc_sub(at, key), key);
}
