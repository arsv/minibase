#include <bits/socket/unix.h>
#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/ppoll.h>
#include <sys/fprop.h>
#include <sys/signal.h>

#include <errtag.h>
#include <format.h>
#include <string.h>
#include <sigset.h>
#include <util.h>

#include "common.h"

ERRTAG("sysklogd");

/* This deamon is made of two distinct parts: syslogd, receiving RFC 3164
   messages on /dev/log socket, and klogd, pulling kernel logs from /proc/kmsg.
   Both sources are piped to the same common log file.

   While it is possible to write standalone klogd, it has to be so much
   dependent on syslogd that it hardly makes any sense. */

#define THRESHOLD (1<<20) /* 1MB, log rotations */
#define TAGSPACE 14 /* see description of storage format below */

struct top {
	int sockfd;      /* /dev/log          */
	int logfd;       /* /var/log/syslog   */
	int klogfd;      /* /proc/kmsg        */
	int late;
	uint64_t size;   /* of the logfile    */
	uint64_t ts;     /* current timestamp */
};

static void sighandler(int sig)
{
	sys_unlink(DEVLOG);
	_exit(-1);
}

static void quit(const char* msg, char* arg, int err)
{
	sys_unlink(DEVLOG);
	fail(msg, arg, err);
}

static void sigaction(struct sigaction* sa, int sig, char* name)
{
	int ret;

	if((ret = sys_sigaction(sig, sa, NULL)) < 0)
		fail("sigaction", name, ret);
}

static void setup_signals(void)
{
	struct sigaction sa = {
		.handler = sighandler,
		.flags = SA_RESTORER,
		.restorer = sigreturn
	};

	sigemptyset(&sa.mask);

	sigaction(&sa, SIGINT,  "SIGINT");
	sigaction(&sa, SIGTERM, "SIGTERM");
	sigaction(&sa, SIGHUP,  "SIGHUP");
};

static void open_logfile(struct top* ctx)
{
	int fd, ret;
	char* name = VARLOG;
	int flags = O_WRONLY | O_CREAT | O_APPEND;
	struct stat st;

	if((fd = sys_open3(name, flags, 0644)) < 0)
		fail("open", name, fd);

	if((ret = sys_fstat(fd, &st)) < 0)
		fail("stat", name, ret);

	ctx->logfd = fd;
	ctx->size = st.size;
};

static void open_socket(struct top* ctx)
{
	int fd, ret;
	int flags = SOCK_DGRAM | SOCK_NONBLOCK;
	struct sockaddr_un addr = {
		.family = AF_UNIX,
		.path = DEVLOG
	};

	if((fd = sys_socket(AF_UNIX, flags, 0)) < 0)
		fail("socket", NULL, fd);

	if((ret = sys_bind(fd, &addr, sizeof(addr))) < 0)
		fail("bind", addr.path, ret);

	if((ret = sys_chmod(addr.path, 0666)) < 0)
		fail("chmod", addr.path, ret);

	ctx->sockfd = fd;
}

void open_klog(struct top* ctx)
{
	int fd;
	char* name = "/proc/kmsg";
	int flags = O_RDONLY | O_NONBLOCK;

	if((fd = sys_open(name, flags)) < 0)
		fail(NULL, name, fd);

	ctx->klogfd = fd;
}

static void maybe_rotate(struct top* ctx)
{
	int ret;

	if(ctx->size < THRESHOLD)
		return;

	if((ret = sys_close(ctx->logfd)) < 0)
		fail("close", VARLOG, ret);

	if((ret = sys_rename(VARLOG, OLDLOG)) < 0)
		fail("rename", VARLOG, ret);

	open_logfile(ctx);
}

static void store_line(struct top* ctx, char* buf, char* end)
{
	char old = *end;
	long len = end - buf + 1;

	*end = '\n';
	writeall(ctx->logfd, buf, len);
	*end = old;

	ctx->size += len;

	maybe_rotate(ctx);
}

static void set_log_time(struct top* ctx)
{
	struct timeval tv;

	sys_gettimeofday(&tv, NULL);

	ctx->ts = tv.sec;
}

/* Syslog part. From the HEADER field, we pick priority but ignore
   timestamp. Client's timestamps are unreliable and there's no point
   in supplying or using them, syslogd can do it just as well.

   There's no way to remove the HOSTNAME part reliably, but luckily
   no-one apparently does this nonsense anymore. Well at least musl
   syslog() and the logger from util-linux don't.

   Note there's also RFC5424 with its own timestamp format but no-one
   apparently uses it, which is probably for the best. */

static int isdigit(int c)
{
	return (c >= '0' && c <= '9');
}

static int isdigsp(int c)
{
	return (c >= '0' && c <= '9') || (c == ' ');
}

static int looks_like_time(char* p)
{
	/* 17:31:21 */
	/* 01234567 */

	if(p[2] != ':' || p[5] != ':')
		return 0;
	if(!isdigit(p[1]) || !isdigit(p[4]) || !isdigit(p[7]))
		return 0;
	if(!isdigsp(p[0]) || !isdigsp(p[3]) || !isdigsp(p[6]))
		return 0;

	return 1;
}

static char* parse_header(char* p, int len, int* prio)
{
	char* r = p;
	char* e = p + len;
	char old = *e;

	if(*p++ != '<')
		goto out;

	*e = '\0';

	if(!(p = parseint(p, prio)))
		goto out;

	*e = old;

	if(*p++ != '>')
		goto out;

	/* Aug 24 17:31:21 */
	/* 012345678901234 */
	if(e - p > 15 && looks_like_time(p + 7))
		p += 15;

	if(*p == ' ') p++;

	r = p;
out:
	return r;
}

static const char fakeys[] = {
	[0] = 'K',	/* kernel */
	[1] = 'U',	/* user */
	[2] = 'M',	/* mail? */
	[3] = 'D',	/* daemon */
	[4] = 'A',	/* auth */
	[5] = 'L',	/* logs */
	[6] = 'P',	/* printer? */
	[7] = 'N',	/* nntp? */
	[8] = 'X',	/* uucp? */
	[9] = 'C',	/* clock */
	[10] = 'A',	/* auth-private */
	[11] = 'F',	/* ftp? */
	[12] = 'T',	/* ntp */
	[13] = 'X',	/* log audit */
	[14] = 'X',	/* log alert */
	[15] = 'C',	/* clock */
	[16] = '0',	/* private use 0 */
	[17] = '1',
	[18] = '2',
	[19] = '3',
	[20] = '4',
	[21] = '5',
	[22] = '6',
	[23] = '7'	/* private use 7 */
};

static char* fmtprio(char* p, char* e, int prio)
{
	int facility = prio >> 3;
	int severity = prio & 7;

	char fc;
	char pc = '0' + severity;

	if(facility >= 0 && facility < sizeof(fakeys))
		fc = fakeys[facility];
	else
		fc = '?';

	p = fmtchar(p, e, fc);
	p = fmtchar(p, e, pc);

	return p;
}

static void remove_controls(char* p, char* e)
{
	for(; p < e; p++)
		if(*p < 0x20) *p = ' ';
}

/* The data we get looks like this:

       <262>Aug 24 14:01:12 foo: some message goes here

   and gets stored like this:

       01234567890123456789012345678901234567890    <-- ruler

       1503571442 D6 foo: some message goes here    <-- stored line

       ^^^^^^^^^^^^^^                               <-- TAGSPACE

   so we recv the original message into a buffer with 14 free bytes
   at the front for the header, remove the protocol header, and put
   the storage header into its place:

       buf           rbuf                 p = msg                    e = end
       v             v                    v                          v
       ..............<262>Aug 24 14:01:12 foo: some message goes here...
       .....................1503571442 U6 foo: some message goes here...
       ^^^^^^^^^^^^^^       ^
          TAGSPACE          s = msg - TAGSPACE

   The protocol prefix may be shorter than that, it may even be missing.
   Either way, there must be at least TAGSPACE bytes in *front* of msg
   (format_tag) or respectively p (send_to_log) for the storage header. */

static char* format_tag(struct top* ctx, int prio, char* msg)
{
	char* s = msg - TAGSPACE; /* !!! */
	char* p = s;
	char* e = msg;

	p = fmtpad0(p, e, 10, fmtu64(p, e, ctx->ts));
	p = fmtchar(p, e, ' ');
	p = fmtprio(p, e, prio);

	while(p < e) *p++ = ' ';

	return s;
}

static void send_to_log(struct top* ctx, int prio, char* p, char* e)
{
	char* s = format_tag(ctx, prio, p);

	remove_controls(p, e);

	store_line(ctx, s, e);
}

static void recv_syslog(struct top* ctx)
{
	int rd, fd = ctx->sockfd;

	int off = TAGSPACE;
	int len = 512;
	char buf[off+len+1];
	char* rbuf = buf + off;

	int prio;

	if((rd = sys_recv(fd, rbuf, len, 0)) < 0)
		quit("recv", "syslog", rd);

	set_log_time(ctx);

	char* msg = parse_header(rbuf, rd, &prio);
	char* end = rbuf + rd;

	send_to_log(ctx, prio, msg, end);
}

/* Klogd part. /proc/kmsg acts just like klogctl(SYSLOG_ACTION_READ)
   but the fd for that file can be ppoll'ed. See syslog(2).

   Unlike syslog socket, this fd may spew several messages at once.
   However, like klogctl, it always reads complete messages.

   klog lines look like this:

       <6>[642493.137018] mmcblk0: mmc0:0007 SD8GB 7.42 GiB

   or possibly like this (depends on kernel config):

       <6>mmcblk0: mmc0:0007 SD8GB 7.42 GiB

   We ignore the [timestamp] field completely even if present.
   It is in seconds relative to something, but it is not clear what is
   that something. Neither boottime nor monotonic clocks yield reliable
   results. Instead, the time recorded is the time when sysklogd gets
   the message. */

static void process_klog_line(struct top* ctx, char* ls, char* le)
{
	char* p = ls;

	if(*p++ != '<') return;
	if(!isdigit(*p++)) return;
	if(*p++ != '>') return;

	if(*p == '[') { /* timestamp is optional! */
		p++;
		while(*p && *p != ']') p++;
		if(*p++ != ']') return;
		if(*p++ != ' ') return;
	}

	/*                  v-- p              */
	/*  <6>[000000.000] some message here  */
	/*   ^-- ls[1]                  le --^ */

	int prio = ls[1] - '0';

	send_to_log(ctx, prio, p, le);
}

static char* eol(char* p, char* e)
{
	while(p < e)
		if(*p == '\n')
			return p;
		else p++;

	return NULL;
}

static void recv_klog(struct top* ctx)
{
	int len = 2048;
	int off = TAGSPACE;
	char buf[off+len+2];

	int rd, fd = ctx->klogfd;

	if((rd = sys_read(fd, buf + off, len)) < 0)
		quit("read", "klog", rd);

	set_log_time(ctx);

	char *p = buf + off;
	char *e = buf + off + rd;
	char *q;

	while(p < e && (q = eol(p, e))) {
		*q = '\0';
		process_klog_line(ctx, p, q);
		p = q + 1;
	}
}

/* The rest is just polling between the syslog socket fd and the klog fd. */

static void set_poll_fd(struct pollfd* pf, int fd)
{
	pf->fd = fd > 0 ? fd : -1;
	pf->events = POLLIN;
}

static int readable(struct pollfd* pf)
{
	return (pf->revents & POLLIN);
}

static int broken(struct pollfd* pf)
{
	return (pf->revents & ~POLLIN);
}

static void poll_loop(struct top* ctx)
{
	struct pollfd pfds[2];
	int ret;

	set_poll_fd(&pfds[0], ctx->sockfd);
	set_poll_fd(&pfds[1], ctx->klogfd);

	while(1) {
		if(!(ret = sys_ppoll(pfds, 2, NULL, NULL)))
			continue;
		if(ret == -EINTR)
			continue;
		if(ret < 0)
			fail("ppoll", NULL, ret);

		if(readable(&pfds[1]))
			recv_klog(ctx);
		if(readable(&pfds[0]))
			recv_syslog(ctx);

		if(broken(&pfds[0]))
			quit("lost syslog socket", NULL, 0);
		if(broken(&pfds[1]))
			quit("lost klog stream", NULL, 0);
	}

}

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;

	if(argc > 1)
		fail("too many arguments", NULL, 0);

	memzero(ctx, sizeof(*ctx));
	setup_signals();

	open_logfile(ctx);
	open_klog(ctx);
	open_socket(ctx);

	maybe_rotate(ctx);

	poll_loop(ctx);

	return 0; /* never reached */
}
