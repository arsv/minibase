#include <sys/timex.h>

#include <main.h>
#include <format.h>
#include <printf.h>
#include <string.h>
#include <util.h>

/* Fetch and dump current clock settings. Also adjust frequency.
   Meant solely for figuring out how adjtimex frequency stuff works. */

ERRTAG("timex");

int main(int argc, char** argv)
{
	struct timex tmx;
	int ret;

	memzero(&tmx, sizeof(tmx));

	if(argc == 2) {
		char* p;
		int freq;

		if(!(p = parseint(argv[1], &freq)) || *p)
			fail("bad frequency", argv[1], 0);

		tmx.modes = ADJ_FREQUENCY;
		tmx.freq = freq;
	} else if(argc > 2) {
		fail("too many arguments", NULL, 0);
	}

	if((ret = sys_adjtimex(&tmx)) < 0)
		fail("adjtimex", NULL, ret);

	tracef("freq = %li\n", tmx.freq);
	tracef("constant = %li\n", tmx.constant);
	tracef("precision = %li\n", tmx.precision);
	tracef("tolerance = %li\n", tmx.tolerance);
	tracef("tick = %li\n", tmx.tick);
	tracef("ppsfreq = %li\n", tmx.ppsfreq);
	tracef("jitter = %li\n", tmx.jitter);
	tracef("shift = %i\n", tmx.shift);
	tracef("stabil = %li\n", tmx.stabil);
	tracef("jitcnt = %li\n", tmx.jitcnt);
	tracef("calcnt = %li\n", tmx.calcnt);
	tracef("errcnt = %li\n", tmx.errcnt);
	tracef("stbcnt = %li\n", tmx.stbcnt);
	tracef("tai = %i\n", tmx.tai);

	return 0;
}
