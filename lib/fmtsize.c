#include <bits/types.h>

#include "fmtchar.h"
#include "fmtint64.h"

#include "fmtsize.h"

static char* fmt1i0(char* p, char* e, int n)
{
	if(p < e)
		*p++ = '0' + (n % 10);
	return p;
}

char* fmtsize(char* p, char* e, uint64_t n)
{
	static const char sfx[] = " KMGTP";
	int sfi = 0;
	int fr = 0;

	/* find out the largest multiplier we can use */
	for(; sfi < sizeof(sfx) && n > 1024; sfi++) {
		fr = n % 1024;
		n /= 1024;
	}
	
	if(sfi >= sizeof(sfx)) {
		/* Too large; format the number and be done with it. */
		p = fmtu64(p, e, (uint32_t)n);
		p = fmtchar(p, e, sfx[sizeof(sfx)-1]);
	} else {
		/* Manageable; do nnnn.d conversion */
		p = fmti64(p, e, n);

		if(sfi > 1 && n < 10) {
			/* No decimals for bytes and Kbytes */
			fr = fr*10/1024; /* one decimal */
			p = fmtchar(p, e, '.');
			p = fmt1i0(p, e, fr);
		} if(sfi > 0)
			p = fmtchar(p, e, sfx[sfi]);
	}

	return p;
}
