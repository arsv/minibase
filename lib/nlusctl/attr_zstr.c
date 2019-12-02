#include <nlusctl.h>
#include <cdefs.h>

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

char* uc_get_str(struct ucattr* msg, int key)
{
	return uc_is_str(uc_get(msg, key), key);
}
