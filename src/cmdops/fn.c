#include <sys/getcwd.h>
#include <sys/chdir.h>
#include <sys/fchdir.h>
#include <sys/write.h>
#include <sys/open.h>
#include <sys/readlink.h>

#include <string.h>
#include <util.h>
#include <fail.h>

ERRTAG = "fn";
ERRLIST = {
	REPORT(EACCES), REPORT(EFAULT), REPORT(EINVAL),
	REPORT(ENAMETOOLONG), REPORT(ENOMEM), REPORT(ENOENT),
	REPORT(ERANGE), REPORT(EIO), REPORT(ELOOP), REPORT(ENOENT),
	REPORT(ENOTDIR), RESTASNUMBERS
};

#define OPTS "abcdr"
#define OPT_a (1<<0)	/* absolutize */
#define OPT_b (1<<1)	/* take basename */
#define OPT_c (1<<2)	/* canonicalize */
#define OPT_d (1<<3)	/* take dirname */
#define OPT_r (1<<4)	/* readlink */

char cwdbuf[4096];

/* Moves end over basename:  /foo//bar₀ -> /foo//bar₀
                        name ^        ^    ^     ^ end  */
static char* revbase(char* name, char* end)
{
	char* p;

	for(p = end; p > name && *(p-1) != '/'; p--)
		;

	return p;
}

/* Moves end over path separator:  /foo//bar₀ -> /foo//bar₀
                              name ^     ^       ^   ^ end  */
static char* revpsep(char* name, char* end)
{
	char* p;

	for(p = end; p > name && *(p-1) == '/'; p--)
		;

	return p;
}

static void printparts(char* name, int len, int opts)
{
	char* end = name + len;
	char* ptr = name;

	if(opts & (OPT_b | OPT_d))
		ptr = revbase(name, end);
	if(opts & OPT_d) {
		end = revpsep(name, ptr);
		ptr = name;
	}

	if(ptr == end && (opts & OPT_d)) {
		ptr = (*name == '/' ? "/\n" : ".\n");
		end = ptr + 2;
	} else {
		*end++ = '\n';
	}

	syswrite(STDOUT, ptr, end - ptr);
}

/* Get rid of multiple slashes and . entries.
   This is always harmless and may be done lexically. */

static int normalize(char* name, int len)
{
	char* p = name;
	char* q = name;
	char last = *p;

	if(last == '/')
		*q++ = *p++;

	while(*p) {
		if(last == '/' && p[0] == '.' && p[1] == '/')
			p += 2;
		else if(last == '/' && *p == '/')
			p++;
		else
			last = *q++ = *p++;
	}

	if(q > name + 2 && *(q-1) == '.' && *(q-2) == '/') {
		q -= 2; *q = '\0';
	}

	return q - name;
}

static void lexical(char* name, int len, int opts)
{
	char buf[len+1];
	memcpy(buf, name, len);
	buf[len] = '\0';

	len = normalize(buf, len);

	printparts(buf, len, opts);
}

static void printatcwd(char* orig, char* name, int namelen, int opts)
{
	if(*name == '/')
		return printparts(name, namelen, opts);

	long cwdlen = xchk(sysgetcwd(cwdbuf, sizeof(cwdbuf)), "getcwd", NULL);

	cwdlen--; /* unlike readlink, getcwd *does* count trailing \0 */

	char fullname[cwdlen + namelen + 2];
	char* p = fullname;
	memcpy(p, cwdbuf, cwdlen+1); p += cwdlen;
	if(namelen) {
		*p++ = '/';
		memcpy(p, name, namelen); p += namelen;
		*p = '\0';
	}

	int fnlen = normalize(fullname, p - fullname);

	printparts(fullname, fnlen, opts);
}

static void abstail(char* orig, char* dir, int dlen, char* tail, int tlen, int opts)
{
	char buf[dlen+1];

	memcpy(buf, dir, dlen);
	buf[dlen] = '\0';

	if(syschdir(buf) < 0)
		fail(NULL, orig, ENOTDIR);

	return printatcwd(orig, tail, tlen, opts);
}

static void absolutize(char* orig, char* name, int namelen, int opts)
{
	char* end = name + namelen;
	char* be = end;
	char* bs;

	while(be > name) {
		bs = revbase(name, be);

		if(!strncmp(bs, "../", 3))
			return abstail(orig, name, bs - name + 2,
					bs + 3, end - bs - 3, opts);
		else if(!strcmp(bs, ".."))
			return abstail(orig, name, bs - name + 2,
					"", 0, opts);

		be = revpsep(name, bs);
	}

	return printatcwd(orig, name, namelen, opts);
}

static void chdirtodirname(char* orig, char* name, int namelen)
{
	char* end = name + namelen;
	char* bn = revbase(name, end);
	char* de = revpsep(name, bn);

	if(*de != '/')
		return;

	long ret;

	if(de == name) {
		ret = syschdir("/");
	} else {
		int dl = de - name;
		char dir[dl+1];
		memcpy(dir, name, dl);
		dir[dl] = '\0';

		ret = syschdir(dir);
	}

	if(ret < 0)
		fail(NULL, orig, ENOTDIR);
}

static void canonlink(char* orig, char* name, int namelen, int opts, int depth)
{
	long ret = sysreadlink(name, cwdbuf, sizeof(cwdbuf));

	if(ret >= 0) {
		/* it's a link, got to follow it */
		if(depth >= 40)
			fail(NULL, orig, ELOOP);

		if(cwdbuf[0] != '/')
			chdirtodirname(orig, name, namelen);

		char link[ret+1];
		memcpy(link, cwdbuf, ret+1);

		canonlink(orig, link, ret, opts, depth + 1);
	} else {
		/* it's not a link */
		chdirtodirname(orig, name, namelen);

		char* end = name + namelen;
		char* bn = revbase(name, end);
		printatcwd(orig, bn, end - bn, opts);
	}
}

static void canonicalize(char* orig, char* name, int namelen, int opts)
{
	if(syschdir(name) >= 0)
		return printatcwd(orig, "", 0, opts);

	char* end = name + namelen;
	char* bn = revbase(name, end);

	if(!*bn || !strcmp(bn, ".") || !strcmp(bn, ".."))
		fail(NULL, orig, ENOTDIR);

	canonlink(orig, name, namelen, opts, 0);
}

static void dereference(char* orig, char* name, int namelen, int opts)
{
	long ret;

	if((ret = sysreadlink(name, cwdbuf, sizeof(cwdbuf))) < 0)
		fail("readlink", orig, ret);

	if(!(opts & (OPT_a | OPT_c)))
		return printparts(cwdbuf, ret, opts);

	char link[ret+1];
	memcpy(link, cwdbuf, ret+1);
	link[ret] = '\0';

	if(link[0] != '/')
		chdirtodirname(orig, name, namelen);

	if(opts & OPT_c)
		canonicalize(orig, link, ret, opts);
	else if(opts & OPT_a)
		absolutize(orig, link, ret, opts);
	else
		printparts(link, ret, opts);
}

static void printfiles(int argc, char** argv, int opts)
{
	long cwdfd = xchk(sysopen(".", O_PATH | O_RDONLY), "open", ".");
	int i;

	for(i = 0; i < argc; i++) {
		if(i) xchk(sysfchdir(cwdfd), "chdir", ".");

		char* arg = argv[i];
		int arglen = strlen(arg);

		if(opts & OPT_r)
			dereference(arg, arg, arglen, opts);
		else if(opts & OPT_c)
			canonicalize(arg, arg, arglen, opts);
		else if(opts & OPT_a)
			absolutize(arg, arg, arglen, opts);
		else
			lexical(arg, arglen, opts);
	}
}

static int adjopts(int opts, int narg)
{
	if((opts & (OPT_b | OPT_d)) == (OPT_b | OPT_d))
		fail("at most one of -bd may be used at once", NULL, 0);

	if(narg <= 0 && (opts & (OPT_r | OPT_c | OPT_a)))
		fail("using -acr without arguments makes no sense", NULL, 0);

	if((opts & (OPT_a | OPT_b | OPT_d | OPT_r)) == OPT_a)
		fail("using -a without -bdr makes no sense", NULL, 0);
	if((opts & (OPT_a | OPT_c)) == (OPT_a | OPT_c))
		fail("using -ac together makes no sense", NULL, 0);

	if(!(opts & (OPT_r | OPT_b | OPT_d)))
		opts |= OPT_a;

	return opts;
}

int main(int argc, char** argv)
{
	int i = 1;
	int opts = 0;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	argc -= i;
	argv += i;

	opts = adjopts(opts, argc);

	if(argc <= 0)
		printatcwd(NULL, "", 0, opts);
	else
		printfiles(argc, argv, opts);

	return 0;
}
