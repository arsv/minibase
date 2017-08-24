#include <bits/socket/unix.h>
#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/signal.h>

#include <format.h>
#include <string.h>
#include <sigset.h>
#include <fail.h>
#include <util.h>
#include <exit.h>

#include "common.h"

#define THRESHOLD (1<<20) /* 1MB */
#define TAGSPACE 13 /* see description of storage format below */

struct top {
	int sockfd;
	int logfd;
	uint64_t size;
};

static void sighandler(int sig)
{
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

static char* parse_header(char* p, int len, int* prio)
{
	char* r = p;
	char old = p[len];

	if(*p++ != '<')
		goto out;

	p[len] = '\0';

	if(!(p = parseint(p, prio)))
		goto out; 

	p[len] = old;

	if(*p++ != '>')
		goto out;
	if(*p == ' ') p++;

	r = p;
out:
	return r;
}

static char* fmt_prio(char* p, char* e, int prio)
{
	int severity = prio & 7;

	return fmtchar(p, e, '0' + severity);
}

static void remove_controls(char* p, char* e)
{
	for(; p < e; p++)
		if(*p < 0x20) *p = ' ';
}

/* The data we get looks like this:

       <262>Aug 24 14:01:12 foo: some message goes here

   and gets stored like this:

       0123456789012345678901234567890123456789    <-- ruler

       1503571442 I foo: some message goes here    <-- stored line

       ^^^^^^^^^^^^^                               <-- TAGSPACE

   so we recv the original message into a buffer with 14 free bytes
   at the front for the header, remove the protocol header, and put
   the storage header into its place:

       .-- buf                             .-- p                  q --.
       v                                   v                          v
       ...............<262>Aug 24 14:01:12 foo: some message goes here...
       ......................1503571442 UI foo: some message goes here...
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
	p = fmt_prio(p, e, prio);

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
