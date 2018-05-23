#include <bits/socket/unix.h>
#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/signal.h>

#include <main.h>
#include <format.h>
#include <string.h>
#include <sigset.h>
#include <util.h>

#include "common.h"

ERRTAG("syslogd");

#define THRESHOLD (1<<20) /* 1MB */
#define TAGSPACE 14 /* see description of storage format below */

struct top {
	int sockfd;
	int logfd;
	uint64_t size;
};

static void sighandler(int sig)
{
	(void)sig;
	sys_unlink(DEVLOG);
	_exit(-1);
}

static void sigaction(struct sigaction* sa, int sig, char* name)
{
	int ret;

	if((ret = sys_sigaction(sig, sa, NULL)) < 0)
		fail("sigaction", name, ret);
}

static void setup_signals(void)
{
	SIGHANDLER(sa, sighandler, 0);

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
	struct sockaddr_un addr = {
		.family = AF_UNIX,
		.path = DEVLOG
	};

	if((fd = sys_socket(AF_UNIX, SOCK_DGRAM, 0)) < 0)
		fail("socket", NULL, fd);

	if((ret = sys_bind(fd, &addr, sizeof(addr))) < 0)
		fail("bind", addr.path, ret);

	ctx->sockfd = fd;
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

/* From the HEADER field (see RFC3164), we pick priority but
   ignore timestamp. Client's timestamps are unreliable and
   there's no point in supplying or using them, syslogd can
   it just as well.

   There's no way to remove the HOSTNAME part reliably, but luckily
   no-one apparently does this nonsense anymore. Well at least musl
   syslog() and logger from util-linux don't.

   There's also RFC5424 but no-one apparently uses it either,
   which is probably for the best. */

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
	[8] = '?',	/* uucp? */
	[9] = 'C',	/* clock */
	[10] = 'A',	/* auth-private */
	[11] = 'F',	/* ftp? */
	[12] = 'T',	/* ntp */
	[13] = '?',	/* log audit */
	[14] = '?',	/* log alert */
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
	uint facility = prio >> 3;
	uint severity = prio & 7;

	char fc;
	char pc = '0' + severity;

	if(facility < sizeof(fakeys))
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

       .-- buf                             .-- p                  q --.
       v                                   v                          v
       ...............<262>Aug 24 14:01:12 foo: some message goes here...
       ......................1503571442 U6 foo: some message goes here...
                             ^
                             t

   The protocol prefix may be shorter than that, it may be even missing,
   so sto may be less than off. */

static char* format_tag(struct timeval* tv, int prio, char* e)
{
	char* s = e - TAGSPACE;
	char* p = s;

	p = fmtpad0(p, e, 10, fmtu64(p, e, tv->sec));
	p = fmtchar(p, e, ' ');
	p = fmtprio(p, e, prio);

	while(p < e) *p++ = ' ';

	return s;
}

static int recvloop(struct top* ctx)
{
	int rd, fd = ctx->sockfd;

	int off = TAGSPACE;
	int len = 512;
	char buf[off+len+1];
	char* rbuf = buf + off;

	struct timeval tv;
	int prio;

	while((rd = sys_recv(fd, rbuf, len, 0)) > 0) {
		sys_gettimeofday(&tv, NULL);

		char* end = rbuf + rd;
		char* msg = parse_header(rbuf, rd, &prio);
		char* tag = format_tag(&tv, prio, msg);

		remove_controls(msg, end);

		store_line(ctx, tag, end);
	}

	return 0;
}

int main(int argc, char** argv)
{
	(void)argv;
	struct top context, *ctx = &context;

	if(argc > 1)
		fail("too many arguments", NULL, 0);

	memzero(ctx, sizeof(*ctx));
	setup_signals();
	open_logfile(ctx);
	open_socket(ctx);
	maybe_rotate(ctx);

	return recvloop(ctx);
}
