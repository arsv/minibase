#include <bits/errno.h>
#include <bits/uname.h>
#include <sys/write.h>
#include <sys/uname.h>

#include <argbits.h>
#include <fail.h>
#include <null.h>
#include <xchk.h>
#include <fmtstr.h>

ERRTAG = "uname";
ERRLIST = {
	REPORT(EFAULT), REPORT(EINVAL), REPORT(ENOSYS),
	REPORT(EBADF), REPORT(EPIPE), RESTASNUMBERS
};

#define OPTS "asnrvmpio"
#define OPT_a (1<<0)
#define OPT_s (1<<1)
#define OPT_n (1<<2)
#define OPT_r (1<<3)
#define OPT_v (1<<4)
#define OPT_m (1<<5)
#define OPT_p (1<<6)
#define OPT_i (1<<7)
#define OPT_o (1<<8)

static char* fmtpart(char* p, char* end, char* part, int on)
{
	if(on) {
		p = fmtstr(p, end, part);
		p = fmtstr(p, end, " ");
	}; return p;
}

static void uname(struct utsname* un, int opts)
{
	char buf[sizeof(struct utsname)];
	char* end = buf + sizeof(buf) - 1;
	char* p = buf;

	p = fmtpart(p, end, un->sysname,  opts & OPT_s);
	p = fmtpart(p, end, un->nodename, opts & OPT_n);
	p = fmtpart(p, end, un->release,  opts & OPT_r);
	p = fmtpart(p, end, un->version,  opts & OPT_v);
	p = fmtpart(p, end, un->machine,  opts & OPT_m);

	p = fmtpart(p, end, "unknown",    opts & OPT_p);
	p = fmtpart(p, end, "unknown",    opts & OPT_i);
	p = fmtpart(p, end, "GNU/Linux",  opts & OPT_o);

	if(p > buf) p--;
	*p++ = '\n';

	xchk(syswrite(1, buf, p - buf), "write", NULL);
}

int main(int argc, char** argv)
{
	int i = 1;
	int opts = 0; 

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);
	if(i < argc)
		fail("too many arguments", NULL, 0);

	if(!opts || (opts & OPT_a))
		opts |= (OPT_s | OPT_n | OPT_r | OPT_v | OPT_m);

	struct utsname un;
	xchk(sysuname(&un), "uname", NULL);
	uname(&un, opts);

	return 0;
}
