#include <sys/nanosleep.h>

#include <string.h>
#include <null.h>

#include "msh.h"
#include "msh_cmd.h"

#define NANOFRAC 1000000000 /* nanoseconds in a second */

/* Valid time specs:
   	123      seconds
	123.456  seconds, with non-zero nanoseconds part
	12m      minutes
	3h       hours
   Suffixing seconds with "s" is also allowed for consistency,
   but "ms" and "ns" are not accepted. */

static int parsetime(struct sh* ctx, struct timespec* sp, char* str)
{
	unsigned long sec = 0;
	unsigned long nsec = 0;
	unsigned long nmul = NANOFRAC;
	const char* p;
	int d;

	if(!*str)
		return error(ctx, "invalid time spec", str, 0);

	/* Integer part */
	for(p = str; *p; p++)
		if(*p >= '0' && (d = *p - '0') < 10)
			sec = sec * 10 + d;
		else
			break;

	/* Suffix (including eos and '.') */
	switch(*p) {
		case 'd': sec *= 24;
		case 'h': sec *= 60;
		case 'm': sec *= 60;
		case 's':
		case '\0':
		case '.': break;
		default:
			  return error(ctx, "invalid time suffix", str, 0);
	}
	
	/* Fractional part, if any */
	if(*p == '.') for(p++; *p; p++) {
		if(*p >= '0' && (d = *p - '0') < 10) {
			if(nmul >= 10) {
				nsec = nsec * 10 + d;
				nmul /= 10;
			}
		} else {
			return error(ctx, "invalid second fraction", str, 0);
		}
	};

	sp->tv_sec = sec;
	sp->tv_nsec = nsec*nmul;

	return 0;
}

/* Several arguments are summed up:

   	sleep 3m 15s -> sleep(195)

   Kind of pointless, but we support suffixes, so why not. */

static int addtime(struct sh* ctx, struct timespec* sp, char* str)
{
	struct timespec ts = { 0, 0 };
	int ret;

	if((ret = parsetime(ctx, &ts, str)))
		return ret;

	sp->tv_nsec += ts.tv_nsec;
	long rem = (sp->tv_nsec > NANOFRAC-1) ? sp->tv_nsec / NANOFRAC : 0;
	sp->tv_sec += ts.tv_sec + rem;

	return 0;
}

int cmd_sleep(struct sh* ctx, int argc, char** argv)
{
	struct timespec sp = { 0, 0 };
	int i, ret;

	if((ret = numargs(ctx, argc, 2, 0)))
		return ret;

	for(i = 1; i < argc; i++)
		if((ret = addtime(ctx, &sp, argv[i])))
			return ret;

	if((ret = sysnanosleep(&sp, NULL)))
		return error(ctx, "sleep", NULL, ret);

	return 0;
}
