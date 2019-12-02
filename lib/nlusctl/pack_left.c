#include <nlusctl.h>

int uc_space_left(struct ucbuf* uc)
{
	struct ucattr* msg = (void*)uc->buf;
	int len = uc->len;
	int ptr = msg->len;

	if(!ptr) return 0; /* overflowed */

	return (len - ptr);
}
