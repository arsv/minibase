#include <bits/ioctl/block.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <sys/ppoll.h>
#include <sys/signal.h>

#include <format.h>
#include <string.h>
#include <sigset.h>
#include <util.h>
#include <main.h>

#include "common.h"
#include "passblk.h"

ERRTAG("passblk");
ERRLIST(NEINVAL NENOENT NENOTTY NEFAULT NENODEV NENOMEM NEPERM NEACCES);

static void check_signal(CTX)
{
	struct siginfo si;
	int fd = ctx->sigfd;
	int ret;

	memzero(&si, sizeof(si));

	if((ret = sys_read(fd, &si, sizeof(si))) < 0)
		quit(ctx, "read", "sigfd", ret);
	if(ret == 0)
		quit(ctx, "signalfd EOF", NULL, 0);

	int sig = si.signo;

	if(sig == SIGINT)
		handle_user_end(ctx);
	if(sig == SIGTERM)
		handle_user_end(ctx);
	if(sig == SIGWINCH)
		handle_sigwinch(ctx);
}

static void check_input(CTX)
{
	char buf[64];
	int fd = STDIN;
	int ret;

	if((ret = sys_read(fd, buf, sizeof(buf))) < 0)
		quit(ctx, "read", "stdin", ret);
	else if(ret == 0)
		handle_user_end(ctx);
	else
		handle_input(ctx, buf, ret);
}

static void parse_part_labels(CTX, int argc, char** argv)
{
	int ki = 0;
	char* sep;
	int i, n = 0;
	int itemp;

	for(i = 2; i < argc; i++) {
		char* arg = argv[i];

		if(arg[0] == '-' && !arg[1]) {
			ki++;
			continue;
		}
		if((sep = parseint(arg, &itemp)) && *sep == ':') {
			if(itemp < 0)
				fail("non-positive key index", NULL, 0);
			else if(!itemp)
				fail("key indexes are 1-based", NULL, 0);

			arg = sep + 1;
			ki = itemp - 1;
		}
		if(ki > NUMKEYS)
			fail("key index out of range:", arg, 0);
		if(n > NUMKEYS)
			fail("too many partitions", NULL, 0);

		struct part* pt = &ctx->parts[n++];

		pt->keyidx = ki;
		pt->label = arg;
	}

	ctx->nparts = n;
}

static void stat_part_node(CTX, struct part* pt)
{
	int fd, ret;
	struct stat st;

	FMTBUF(p, e, path, 100);
	p = fmtstr(p, e, "/dev/mapper/");
	p = fmtstr(p, e, pt->label);
	FMTEND(p, e);

	if((fd = sys_open(path, O_RDONLY)) < 0)
		fail(NULL, path, fd);
	if((ret = sys_fstat(fd, &st)) < 0)
		fail(NULL, path, ret);
	if((ret = sys_ioctl(fd, BLKGETSIZE64, &pt->size)))
		fail("ioctl BLKGETSIZE64", path, ret);

	pt->rdev = st.rdev;
}

static void validate_parts(CTX)
{
	int i, n = ctx->nparts;

	for(i = 0; i < n; i++) {
		struct part* pt = &ctx->parts[i];

		if(pt->keyidx >= ctx->nkeys)
			fail("no key in keyfile for", pt->label, 0);

		stat_part_node(ctx, pt);
	}
}

static void setup_args(CTX, int argc, char** argv)
{
	if(argc < 3)
		fail("too few arguments", NULL, 0);

	parse_part_labels(ctx, argc, argv);

	load_key_data(ctx, argv[1]);

	validate_parts(ctx);
}

static void open_sigfd(CTX)
{
	int fd, ret;
	sigset_t mask;

	sigemptyset(&mask);
	sigaddset(&mask, SIGWINCH);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGTERM);

	if((fd = sys_signalfd(-1, &mask, SFD_NONBLOCK)) < 0)
		fail("signalfd", NULL, fd);
	if((ret = sys_sigprocmask(SIG_BLOCK, &mask, NULL)) < 0)
		fail("sigprocmask", NULL, ret);

	ctx->sigfd = fd;
}

static void prepare_poll(CTX, struct pollfd pfds[2])
{
	pfds[0].fd = STDIN;
	pfds[0].events = POLLIN;

	pfds[1].fd = ctx->sigfd;
	pfds[1].events = POLLIN;
}

static void poll(CTX, struct pollfd pfds[2])
{
	int ret;
	int wait = ctx->needwait;
	struct timespec* ts = wait ? &ctx->ts : NULL;

	if((ret = sys_ppoll(pfds, 2, ts, NULL)) < 0)
		quit(ctx, "ppoll", NULL, ret);

	if(ret == 0)
		handle_timeout(ctx);
	if(pfds[0].revents & POLLIN)
		check_input(ctx);
	if(pfds[1].revents & POLLIN)
		check_signal(ctx);
}

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;
	struct pollfd pfds[2];

	memzero(ctx, sizeof(*ctx));

	setup_args(ctx, argc, argv);
	open_sigfd(ctx);
	alloc_scrypt(ctx);
	init_mapper(ctx);

	prepare_poll(ctx, pfds);

	start_terminal(ctx);

	while(1) poll(ctx, pfds);
}
