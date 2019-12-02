#include <cdefs.h>
#include <nlusctl.h>

void* uc_is_bin(struct ucattr* at, int key, int len)
{
	if(!at || at->key != key)
		return NULL;
	if(uc_paylen(at) != len)
		return NULL;

	return uc_payload(at);
}

void* uc_get_bin(struct ucattr* msg, int key, int len)
{
	return uc_is_bin(uc_get(msg, key), key, len);
}

int* uc_is_int(struct ucattr* at, int key)
{
	return uc_is_bin(at, key, 4);
}

int* uc_get_int(struct ucattr* msg, int key)
{
	return uc_is_bin(uc_get(msg, key), key, 4);
}

int64_t* uc_get_i64(struct ucattr* msg, int key)
{
	return uc_is_bin(uc_get(msg, key), key, 8);
}

uint64_t* uc_get_u64(struct ucattr* msg, int key)
{
	return uc_is_bin(uc_get(msg, key), key, 8);
}
