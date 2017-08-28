#include <bits/socket/unix.h>

#include <sys/socket.h>
#include <sys/file.h>
#include <sys/klog.h>
#include <sys/sched.h>

#include <errtag.h>
#include <string.h>
#include <util.h>

#include "common.h"

ERRTAG("klogd");
ERRLIST(NEINVAL NEFAULT NEINTR);

struct top {
	int fd;
	int late;
};

/* klogd is a special case of a syslog client as it pipes a different log
   source (the kernel ring buf) into syslog destructively. Because of that,
   it should avoid dropping the data received from the kernel.
   
   During startup, klogd may come up before syslogd does, so it should wait
   a bit. Later syslogd may get restarted, and if that happens klogd should
   re-connect.

   Note that while klodg may sleep a bit, it should try to stay below what
   super(8) considers a successful run time, so that if it keeps failing
   it would eventually get disabled.
 
   [ This all means klogd should be merged into syslogd really, but that ]
   [ would require reading from /proc/kmsg instead of calling klogctl.   ] */

static void sleep(int sec)
{
	struct timespec ts = { sec, 0 };

	sys_nanosleep(&ts, NULL);
}

static void open_socket(struct top* ctx)
{
	int i, fd, ret;

	struct sockaddr_un addr = {
		.family = AF_UNIX,
		.path = DEVLOG
	};

	if((fd = sys_socket(AF_UNIX, SOCK_DGRAM, 0)) < 0)
		fail("socket", NULL, fd);

	for(i = 0; i < 3; i++)
		if((ret = sys_connect(fd, &addr, sizeof(addr))) >= 0)
			break;
		else sleep(1);
	if(ret < 0)
		fail("connect", addr.path, ret);

	ctx->fd = fd;
}

static void reopen_socket(struct top* ctx)
{
	sys_close(ctx->fd);
	open_socket(ctx);
}

static void send_to_log(struct top* ctx, char* msg, int len)
{
	int ret;

	if((ret = sys_send(ctx->fd, msg, len, 0)) >= 0)
		return;

	sleep(1);
	reopen_socket(ctx);

	if((ret = sys_send(ctx->fd, msg, len, 0)) >= 0)
		return;

	fail("send", NULL, ret);
}

/* For the sake of log readability, we place an artificial message
   to indicate the klogd detected a newly-booted kernel. */

static void maybe_report_boot(struct top* ctx, char* ts)
{
	char* p;

	if(ctx->late)
		return;

	ctx->late = 1;

	for(p = ts; *p; p++)
		if(*p != '0')
			break;
	if(*p != '.' && *p != ']')
		return;

	char* msg = "<1> --- SYSTEM BOOT ---";

	send_to_log(ctx, msg, strlen(msg));
}

static int isdigit(int c)
{
	return (c >= '0' && c <= '9');
}

/* Kernel log messages come with a ready to use priority field,
   so klogd does not parse it but copies as is. Kernel timestamps
   are removed -- they are unreliable at best, and current syslogd
   implementation would drop them anyway.

       <6>[589139.122406] wlp1s0: associated₀
       ^                                    ^
       ls                                  le

   The line is always 0-terminated but le is already there and
   makes it easier to get the final msg length for send_to_log(). */

static void process_line(struct top* ctx, char* ls, char* le)
{
	char* p = ls;

	if(*p++ != '<') return;
	if(!isdigit(*p++)) return;
	if(*p++ != '>') return;

	if(*p++ != '[') return;
	while(*p && *p != ']') p++;
	if(*p++ != ']') return;

	if(*p++ != ' ') return;

	maybe_report_boot(ctx, ls + 4);

	/*                  v-- p             */
	/*  <6>[000000.000] some message here */
	/*  ............<6> some message here */
	/*    (p - 4) --^                     */

	memcpy(p -= 4, ls, 3);

	send_to_log(ctx, p, le - p);
}

static char* eol(char* p, char* e)
{
	while(p < e)
		if(*p == '\n')
			return p;
		else p++;

	return NULL;
}

/* klogctl always returns complete lines, and rd < len is typical */

static void pipe_klog_to_syslog(struct top* ctx)
{
	int len = 3000;
	char buf[len+2];

	int cmd = SYSLOG_ACTION_READ;
	int rd;

	while((rd = sys_klogctl(cmd, buf, len)) > 0) {

		char *p = buf;
		char *e = buf + rd;
		char *q;

		while(p < e && (q = eol(p, e))) {
			*q = '\0';
			process_line(ctx, p, q);
			p = q + 1;
		}

	} if(rd < 0) {
		fail("klogctl", NULL, rd);
	}
}

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;

	if(argc > 1)
		fail("too many arguments", NULL, 0);

	open_socket(ctx);

	pipe_klog_to_syslog(ctx);

	return 0;
}
