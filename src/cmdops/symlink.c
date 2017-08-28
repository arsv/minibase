#include <sys/file.h>
#include <sys/fpath.h>

#include <errtag.h>
#include <string.h>
#include <util.h>

/* Simple symlinking tool, follows the kernel syscall */

#define OPTS "fxtm"
#define OPT_f (1<<0)
#define OPT_x (1<<1)
#define OPT_t (1<<2)
#define OPT_m (1<<3)

ERRTAG("symlink");
ERRLIST(NEACCES NEDQUOT NEEXIST NEFAULT NEIO NELOOP NENOENT NENOMEM
	NENOSPC NENOTDIR NEPERM NEROFS NEBADF);

static int issymlink(char* file)
{
	struct stat st;

	long ret = sys_lstat(file, &st);

	return (ret >= 0 && ((st.mode & S_IFMT) == S_IFLNK));
}

/* The syscall has reversed argument order! */

static void symlink(char* linkname, char* target, int opts)
{
	long ret = sys_symlink(target, linkname);

	if(ret >= 0)
		return;
	if(ret != -EEXIST || !(opts & (OPT_f | OPT_x)))
		fail(NULL, linkname, ret);

	if(opts & OPT_f && !issymlink(linkname))
		fail("refusing to overwrite", linkname, 0);

	xchk(sys_unlink(linkname), "cannot unlink", linkname);
	xchk(sys_symlink(target, linkname), NULL, linkname);
}

static void symlinkto(char* dir, int argc, char** argv, int opts)
{
	int i;
	int dlen = strlen(dir);

	for(i = 0; i < argc; i++) {
		char* target = argv[i];
		char* base = basename(target);
		int blen = strlen(base);

		char linkname[dlen+blen+2];
		char* p = linkname;
		memcpy(p, dir, dlen); p += dlen; *p++ = '/';
		memcpy(p, base, blen); p += blen; *p = '\0';

		symlink(linkname, target, opts);
	}
}

static void symlinkmt(char* dir, char* target, int argc, char** argv, int opts)
{
	int i;
	int dlen = strlen(dir);

	for(i = 0; i < argc; i++) {
		char* base = basename(argv[i]);
		int blen = strlen(base);

		char linkname[dlen+blen+2];
		char* p = linkname;
		memcpy(p, dir, dlen); p += dlen; *p++ = '/';
		memcpy(p, base, blen); p += blen; *p = '\0';

		symlink(linkname, target, opts);
	}
}

int main(int argc, char** argv)
{
	int i = 1;
	int opts = 0;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	int minargs = ((opts & OPT_m) ? 3 : 2);

	argc -= i;
	argv += i;

	if((opts & (OPT_t | OPT_m)) == (OPT_t | OPT_m))
		fail("cannot use -m and -t at the same time", NULL, 0);
	if(argc < minargs)
		fail("too few arguments", NULL, 0);
	if(opts & OPT_m)
		symlinkmt(argv[0], argv[1], argc - 2, argv + 2, opts);
	if(opts & OPT_t)
		symlinkto(argv[0], argc - 1, argv + 1, opts);
	else if(argc > 2)
		fail("too many arguments", NULL, 0);
	else
		symlink(argv[0], argv[1], opts);

	return 0;
}
