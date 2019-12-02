#include <nlusctl.h>

void uc_put_int(struct ucbuf* uc, int key, int v)
{
	struct ucattr* at;
	int size = sizeof(int);

	if(!(at = uc_put(uc, key, size, size)))
		return;

	int* p = (void*)(at->payload);

	*p = v;
}

void uc_put_i64(struct ucbuf* uc, int key, int64_t v)
{
	struct ucattr* at;
	int size = sizeof(int64_t);

	if(!(at = uc_put(uc, key, size, size)))
		return;

	int64_t* p = (void*)(at->payload);

	*p = v;
}
