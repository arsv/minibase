#include <nlusctl.h>

/* Nested attributes; uc_put_nest is basically uc_put_flag that does return
   the attr, but it's a special use case so it's kept separate. */

struct ucattr* uc_put_nest(struct ucbuf* uc, int key)
{
	return uc_put(uc, key, 0, 0);
}

void uc_end_nest(struct ucbuf* uc, struct ucattr* at)
{
	void* buf = uc->buf;
	void* end = buf + uc->len;
	void* atp = at;

	if(!atp)
		return; /* uc_put_nest overflowed uc */
	if(atp < buf)
		return;
	if(atp >= end)
		return;

	struct ucattr* msg = buf;

	int ptr = msg->len;
	int off = (atp - buf);

	if(off >= ptr)
		return;

	at->len = ptr - off;
}
