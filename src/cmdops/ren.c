#include <sys/fpath.h>

#include <string.h>
#include <util.h>
#include <main.h>

#define OPTS "ftwx"
#define OPT_f (1<<0)   /* no overwrites */
#define OPT_t (1<<1)   /* target dir first */
#define OPT_w (1<<2)   /* whiteout */
#define OPT_x (1<<3)   /* exchange atomically */

ERRTAG("ren");
ERRLIST(NEACCES NEBUSY NEFAULT NEINVAL NEISDIR NELOOP NEMLINK NENOENT
	NENOMEM NENOSPC NENOTDIR NENOTEMPTY NEEXIST NEPERM NEROFS NEXDEV);

static void rename(char* src, char* dst, int flags)
{
	int ret;

	if((ret = sys_renameat2(AT_FDCWD, src, AT_FDCWD, dst, flags)) < 0)
		fail("cannot rename", src, ret);
}

static void movetodir(char* dir, int argc, char** argv, int flags)
{
	int i;
	int dlen = strlen(dir);

	for(i = 0; i < argc; i++) {
		char* base = basename(argv[i]);
		int blen = strlen(base);

		char fullname[dlen+blen+2];
		char* p = fullname;
		memcpy(p, dir, dlen); p += dlen; *p++ = '/';
		memcpy(p, base, blen); p += blen; *p = '\0';

		rename(argv[i], fullname, flags);
	}
}

int main(int argc, char** argv)
{
	int i = 1;
	int opts = 0;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	argc -= i;
	argv += i;

	if(argc < 2)
		fail("too few arguments", NULL, 0);

	if((opts & OPT_x) && (opts & (OPT_f | OPT_w)))
		fail("cannot -fw with -x", NULL, 0);

	int flags = (opts & OPT_f ? 0 : RENAME_NOREPLACE)
	          | (opts & OPT_x ? RENAME_EXCHANGE : 0)
	          | (opts & OPT_w ? RENAME_WHITEOUT : 0);

	if(opts & OPT_t)
		movetodir(argv[0], argc - 1, argv + 1, flags);
	else if(argc > 2)
		fail("too many arguments", NULL, 0);
	else
		rename(argv[0], argv[1], flags);
	
	return 0;
};
