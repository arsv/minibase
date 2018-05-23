#include <sys/fpath.h>

#include <string.h>
#include <util.h>
#include <main.h>

#define PARENTS (1<<0)
#define SETMODE (1<<1)

ERRTAG("mkdir");
ERRLIST(NEACCES NEEXIST NEFAULT NELOOP NEMLINK NENOENT NENOMEM
	NENOSPC NENOSPC NENOTDIR NEPERM NEROFS);

static int parseopts(const char* opts)
{
	const char* p;
	char opt[] = "-?";
	int ret = 0;

	for(p = opts; *p; p++)
		switch(*p) {
			case 'p': ret |= PARENTS; break;
			case 'm': ret |= SETMODE; break;
			default:
				opt[1] = *p;
				fail("unknown option", opt, 0);
		}

	return ret;
}

static int parsemode(const char* mode)
{
	const char* p;
	int d, m = 0;

	for(p = mode; *p; p++)
		if(*p >= '0' && (d = *p - '0') < 8)
			m = (m<<3) | d;
		else
			fail("bad mode specification", mode, 0);

	return m;
}

static void makedir(char* name, int mode)
{
	long ret = sys_mkdir(name, mode);

	if(ret >= 0 || ret == -EEXIST)
		return;

	fail("cannot create", name, ret);
}

static void makeall(char* name, int mode)
{
	char* p = name;
	char* q = name;
	char s;

	for(;;) {
		q = strcbrk(p, '/');

		s = *q; *q = '\0';
		makedir(name, mode);
		*q = s; p = q + 1;

		if(!s) break;
	};
}

int main(int argc, char** argv)
{
	int opts = 0;
	int mode = 0777;
	int i = 1;

	if(i < argc && argv[i][0] == '-')
		opts = parseopts(argv[i++] + 1);

	if(i < argc && (opts & SETMODE))
		mode = parsemode(argv[i++]);

	if(i >= argc)
		fail("no directories to create", NULL, 0);

	while(i < argc)
		if(opts & PARENTS)
			makeall(argv[i++], mode);
		else
			makedir(argv[i++], mode);
	
	return 0;
}
