#include <nlusctl.h>
#include <string.h>

struct ucattr* uc_put_strs(struct ucbuf* uc, int key)
{
	return uc_put(uc, key, 0, 0);
}

void uc_add_str(struct ucbuf* uc, const char* str)
{
	void* buf = uc->buf;
	int cap = uc->len;
	struct ucattr* msg = buf;
	int ptr = msg->len;

	int len = strlen(str) + 1;

	if(ptr + len > cap) { /* overflow */
		msg->len = 0;
		return;
	}

	memcpy(buf + ptr, str, len);
	msg->len = ptr + len;
}

void uc_end_strs(struct ucbuf* uc, struct ucattr* at)
{
	void* buf = uc->buf;
	void* atp = at;

	if(!atp) return; /* overflowed */

	struct ucattr* msg = buf;

	int ats = atp - buf;   /* start of at, bytes into uc */
	int ptr = msg->len;    /* current end of at, bytes into uc */
	int len = ptr - ats;   /* current length of at */

	if(len < sizeof(*at)) return; /* bogus pointers */

	at->len = len;

	ptr = (ptr + 3) & ~3; /* pad attribute if necessary */

	if(ptr > uc->len) /* no space left for padding overflow */
		msg->len = 0;
	else
		msg->len = ptr;
}
