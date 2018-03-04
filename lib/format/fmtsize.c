#include <bits/types.h>
#include <printf.h>
#include <format.h>

char* fmtsize(char* p, char* e, uint64_t n)
{
	static const char sfx[] = " KMGTP";
	static const unsigned maxi = sizeof(sfx) - 2; /* stop at P */
	unsigned sfi = 0;
	unsigned fr = 0;

	/* find out the largest multiplier we can use */
	while(n >= 1024 && sfi < maxi) {
		fr = n % 1024;
		n /= 1024;
		sfi++;
	}

	if(sfi < 1) { /* bytes */
		return fmtu64(p, e, n);
	} else if(sfi == 1) { /* KB; round to nearest integer */
		if(fr >= 512) n++;
		p = fmtu64(p, e, n);
		p = fmtchar(p, e, sfx[sfi]);
	} else if(n < 1024) { /* MB to PB */
		if(n >= 10) { /* round to nearest integer for large n */
			if(fr >= 512)
				n++;
			p = fmti64(p, e, n);
		} else { /* only show decimals if the n is short */
			fr = ((fr*100)/1024 + 5)/10;
			if(fr >= 10) {
				n++;
				fr = 0;
			}
			p = fmti64(p, e, n);
			p = fmtchar(p, e, '.');
			p = fmtchar(p, e, '0' + fr);
		}

		p = fmtchar(p, e, sfx[sfi]);
	} else { /* Too large; return suffixed integer. */
		if(fr >= 512) n++;
		p = fmtu64(p, e, n);
		p = fmtchar(p, e, sfx[maxi]);
	}

	return p;
}
