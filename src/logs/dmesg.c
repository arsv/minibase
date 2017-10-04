#include <sys/klog.h>
#include <sys/mman.h>

#include <string.h>
#include <format.h>
#include <output.h>
#include <errtag.h>
#include <util.h>

/* This tool was originally much larger and included colored output
   code. All that stuff subsequently migrated to logcat, and logcat
   is now assumed to be the primary way of reading the log.

   dmesg remains as a low-level debug utility for working on the raw
   ring buffer. So it does not do any text transformation anymore. */

#define OPTS "cn"
#define OPT_c (1<<0)	/* clear kernel ring buffer */
#define OPT_n (1<<1)	/* set log level instead of reading KRB */

#define OUTBUF (4*1024)
#define MINLOGBUF (16*1024)
#define MAXLOGBUF (16*1024*1024)

ERRTAG("dmesg");
ERRLIST(NEINVAL NEFAULT NENOSYS NEPERM);

static void dump_contents(int clear)
{
	long size, len;

	if((size = sys_klogctl(SYSLOG_ACTION_SIZE_BUFFER, NULL, 0)) < 0)
		fail("klogctl", "SIZE_BUFFER", size);

	int act = clear ? SYSLOG_ACTION_READ_CLEAR : SYSLOG_ACTION_READ_ALL;
	char* actname = clear ? "READ_CLEAR" : "READ_ALL";

	char* buf = sys_brk(0);
	char* end = sys_brk(buf + size + 1);

	if(brk_error(buf, end))
		fail("cannot allocate memory", NULL, 0);

	if((len = sys_klogctl(act, buf, size)) < 0)
		fail("klogctl", actname, len);
	
	if(len && buf[len] != '\n')
		buf[len++] = '\n';

	writeall(STDOUT, buf, len);
}

static void set_loglevel(char* arg)
{
	int ret;

	if(!arg[0] || arg[2] || arg[1] < '0' || arg[1] > '9')
		fail("bad level spec:", arg, 0);

	int lvl = arg[1] = '0';

	if((ret = sys_klogctl(SYSLOG_ACTION_CONSOLE_LEVEL, NULL, lvl)) < 0)
		fail("klogctl", "CONSOLE_LEVEL", ret);
}

static void no_more_args(int argc, char** argv, int i)
{
	if(i < argc)
		fail("too many arguments", NULL, 0);
}

static char* get_single_arg(int argc, char** argv, int i)
{
	if(i >= argc)
		fail("argument required", NULL, 0);

	char* ret = argv[i++];

	no_more_args(argc, argv, i);

	return ret;
}

int main(int argc, char** argv)
{
	int i = 1, opts = 0;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	if(opts & OPT_n) {
		char* arg = get_single_arg(argc, argv, i);
		set_loglevel(arg);
	} else {
		no_more_args(argc, argv, i);
		dump_contents(opts & OPT_c);
	}

	return 0;
}
