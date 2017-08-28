#include <bits/ioctl/fstrim.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/sync.h>

#include <errtag.h>
#include <util.h>

#define OPTS "fdt"
#define OPT_f (1<<0) /* syncfs */
#define OPT_d (1<<1) /* datasync */
#define OPT_t (1<<2) /* fstrim */

ERRTAG("sync");
ERRLIST(NEBADF NEIO NEROFS NEINVAL NEACCES NENOENT NEFAULT NEFBIG NEINTR
	NELOOP NENFILE NEMFILE NENODEV NENOMEM NENOTDIR NEPERM NEWOULDBLOCK);

static int open_ref(char* name)
{
	const int flags = O_RDONLY | O_NONBLOCK;

	return xchk(sys_open(name, flags), NULL, name);
}

static void simplesync(int argc, char** argv, int i)
{
	if(i >= argc)
		xchk(sys_sync(), "sync", NULL);
	else for(; i < argc; i++)
		xchk(sys_fsync(open_ref(argv[i])), NULL, argv[i]);
}

static void fdatasync(int argc, char** argv, int i)
{
	if(i >= argc)
		fail("too few arguments", NULL, 0);
	for(; i < argc; i++)
		xchk(sys_fdatasync(open_ref(argv[i])), NULL, argv[i]);
}

static void syncfs(int argc, char** argv, int i)
{
	if(i >= argc)
		fail("too few arguments", NULL, 0);

	char* name = argv[i++];

	if(i >= argc)
		fail("too many arguments", NULL, 0);

	int fd = open_ref(name);

	xchk(sys_syncfs(fd), NULL, name);
}

static void parsesuffixed(uint64_t* u, const char* n)
{
	uint64_t tmp = 0;
	const char* p;
	int d;

	for(p = n; *p; p++)
		if(*p >= '0' && (d = *p - '0') <= 9)
			tmp = tmp*10 + d;
		else
			break;
	switch(*p)
	{
		default: fail("invalid number", n, 0);
		case '\0': break;
		case 'G': tmp *= 1024;
		case 'M': tmp *= 1024;
		case 'K': tmp *= 1024;
	};

	*u = tmp;
}

static void fstrim(int argc, char** argv, int i)
{
	struct fstrim_range range = { 0, (uint64_t)-1, 0 };

	if(i >= argc)
		fail("too few arguments", NULL, 0);

	char* name = argv[i++];

	if(i < argc)
		parsesuffixed(&range.minlen, argv[i++]);
	if(i < argc)
		parsesuffixed(&range.start, argv[i++]);
	if(i < argc)
		parsesuffixed(&range.len, argv[i++]);
	if(i < argc)
		fail("too many arguments", NULL, 0);

	long fd = open_ref(name);

	xchk(sys_ioctl(fd, FITRIM, &range), NULL, name);
}

int main(int argc, char** argv)
{
	int i = 1;
	int opts = 0;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	if(!opts)
		simplesync(argc, argv, i);
	else if(opts == OPT_t)
		fstrim(argc, argv, i);
	else if(opts == OPT_d)
		fdatasync(argc, argv, i);
	else if(opts == OPT_f)
		syncfs(argc, argv, i);

	return 0;
}
