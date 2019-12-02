#include <nlusctl.h>
#include <cdefs.h>

/* This is just a case of short put in terms of implementation, but it's
   kept separate because of how different its use is from other short-put
   routines. It can only come last, and should be followed by send_iov
   which is never used with anything else. */

void uc_put_tail(struct ucbuf* uc, int key, int paylen)
{
	(void)uc_put(uc, key, paylen, 0);
}
