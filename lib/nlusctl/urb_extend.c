#include <nlusctl.h>
#include <string.h>

void ur_buf_change(struct urbuf* ur, void* newbuf, size_t newlen)
{
	void* oldbuf = ur->buf;
	void* mptr = ur->mptr;
	void* rptr = ur->rptr;

	long moff = mptr - oldbuf;
	long roff = rptr - oldbuf;

	if(roff > 0)
		memcpy(newbuf, oldbuf, roff);

	ur->buf = newbuf;
	ur->mptr = newbuf + moff;
	ur->rptr = newbuf + roff;
	ur->end = newbuf + newlen;

	ur->msg = NULL;
}
