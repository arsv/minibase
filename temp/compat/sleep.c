#include <sys/file.h>
#include <sys/sched.h>

#include <string.h>
#include <util.h>

#define NANOFRAC 1000000000 /* nanoseconds in a second */

/* There's no errno reporting in sleep, so no point in linking fail(). */

static void fail2(const char* msg, const char* obj)
{
	unsigned msglen = strlen(msg);
	unsigned objlen = obj ? strlen(obj) : 0;
	char buf[msglen+objlen+2];
	char* p = buf;

	memcpy(p, msg, msglen); p += msglen;
	if(!obj) goto nl;

	*p++ = ' ';
	memcpy(p, obj, objlen); p += objlen;

nl:	*p++ = '\n';
	sys_write(2, buf, p - buf);
	_exit(255);
}

/* Valid time specs:
   	123      seconds
	123.456  seconds, with non-zero nanoseconds part
	12m      minutes
	3h       hours
   Suffixing seconds with "s" is also allowed for consistency,
   but "ms" and "ns" are not accepted. */

static void parsetime(struct timespec* sp, const char* str)
{
	unsigned long sec = 0;
	unsigned long nsec = 0;
	unsigned long nmul = NANOFRAC;
	const char* p;
	int d;

	if(!*str) fail2("invalid time spec", str);

	/* Integer part */
	for(p = str; *p; p++)
		if(*p >= '0' && (d = *p - '0') < 10)
			sec = sec * 10 + d;
		else
			break;

	/* Suffix (including eos and '.') */
	switch(*p) {
		case 'd': sec *= 24; /* fallthrough */
		case 'h': sec *= 60; /* fallthrough */
		case 'm': sec *= 60; /* fallthrough */
		case 's':
		case '\0':
		case '.': break;
		default: fail2("invalid time suffix", str);
	}
	
	/* Fractional part, if any */
	if(*p == '.') for(p++; *p; p++) {
		if(*p >= '0' && (d = *p - '0') < 10) {
			if(nmul >= 10) {
				nsec = nsec * 10 + d;
				nmul /= 10;
			}
		} else {
			fail2("invalid second fraction", str);
		}
	};

	sp->sec = sec;
	sp->nsec = nsec*nmul;
}

/* Several arguments are summed up:

   	sleep 3m 15s -> sleep(195)

   Kind of pointless, but we support suffixes, so why not. */

static void addtime(struct timespec* sp, const char* str)
{
	struct timespec ts = { 0, 0 };

	parsetime(&ts, str);

	sp->nsec += ts.nsec;
	long rem = (sp->nsec > NANOFRAC-1) ? sp->nsec / NANOFRAC : 0;
	sp->sec += ts.sec + rem;
}

int main(int argc, char** argv)
{
	int i = 1;
	struct timespec sp = { 0, 0 };

	if(i >= argc)
		fail2("missing argument", NULL);
	else for(; i < argc; i++)
		addtime(&sp, argv[i]);

	long ret = sys_nanosleep(&sp, NULL);

	return (ret < 0 ? -1 : 0);
}
