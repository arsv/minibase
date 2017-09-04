#include <bits/socket/unix.h>
#include <bits/major.h>

#include <sys/socket.h>
#include <sys/signal.h>
#include <sys/creds.h>
#include <sys/dents.h>
#include <sys/file.h>

#include <errtag.h>
#include <nlusctl.h>
#include <sigset.h>
#include <cmsg.h>
#include <string.h>
#include <format.h>
#include <output.h>
#include <util.h>
#include <heap.h>

#include "common.h"

ERRTAG("pmount");
ERRLIST(NEACCES NEAGAIN NEALREADY NEBADF NEBADMSG NEBUSY NECONNREFUSED
	NEEXIST NEFAULT NEFBIG NEINVAL NEIO NEISDIR NELOOP NEMFILE NENFILE
	NENAMETOOLONG NENOBUFS NENODEV NENOENT NENOMEM NENOSPC NENOSYS
	NENOTCONN NENOTDIR NENOTTY NENXIO NEPERM NEROFS NENOTBLK);

#define OPTS "fugr"
#define OPT_f (1<<0)
#define OPT_u (1<<1)
#define OPT_g (1<<2)
#define OPT_r (1<<3)

char txbuf[3072];
char rxbuf[32];
char ancillary[128];
int signal;

struct top {
	int argc;
	char** argv;
	int argi;
	int opts;
};

#define CTX struct top* ctx

int init_socket(void)
{
	int fd, ret;
	struct sockaddr_un addr = {
		.family = AF_UNIX,
		.path = CONTROL
	};

	if((fd = sys_socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
		fail("socket", "AF_UNIX", fd);
	if((ret = sys_connect(fd, &addr, sizeof(addr))) < 0)
		fail("connect", addr.path, ret);

	return fd;
}

static void send_simple(int fd, char* buf, int len)
{
	int ret;

	if((ret = sys_send(fd, buf, len, 0)) < 0)
		fail("send", NULL, ret);
}

static int put_cmsg_fd(char* buf, int size, int fd)
{
	char* p = buf;
	char* e = buf + size;

	p = cmsg_put(p, e, SOL_SOCKET, SCM_RIGHTS, &fd, sizeof(fd));

	return p - buf;
}

static void send_with_anc(int fd, char* txbuf, int txlen, char* anc, int anlen)
{
	struct iovec iov = {
		.base = txbuf,
		.len = txlen
	};

	struct msghdr msg = {
		.iov = &iov,
		.iovlen = 1,
		.control = anc,
		.controllen = anlen
	};

	xchk(sys_sendmsg(fd, &msg, 0), "send", NULL);
}

static void recv_reply(int fd, int nlen)
{
	char rxbuf[80+nlen];
	struct ucmsg* msg;
	int rd;

	if((rd = sys_recv(fd, rxbuf, sizeof(rxbuf), 0)) < 0)
		fail("recv", NULL, rd);
	if(!(msg = uc_msg(rxbuf, rd)))
		fail("recv", NULL, -EBADMSG);
	if(msg->cmd == -ENODATA)
		fail("no valid filesystem found", NULL, 0);
	if(msg->cmd < 0)
		fail(NULL, NULL, msg->cmd);
	if(msg->cmd > 0)
		fail("unexpected reply", NULL, 0);

	char* mountpoint;

	if(!(mountpoint = uc_get_str(msg, ATTR_PATH)))
		return;

	int mplen = strlen(mountpoint);
	mountpoint[mplen] = '\n';

	sys_write(STDOUT, mountpoint, mplen + 1);
}

static int open_rw_or_ro(char* name)
{
	int fd;

	if((fd = sys_open(name, O_RDWR)) >= 0)
		return fd;
	if(fd != -EACCES)
		goto err;
	if((fd = sys_open(name, O_RDONLY)) >= 0)
		return fd;
err:
	fail(NULL, name, fd);
}

static void cmd_name_fd(int cmd, char* name, int ffd)
{
	int nlen = strlen(name);
	int sfd = init_socket();

	char txbuf[20 + nlen];
	struct ucbuf uc = {
		.brk = txbuf,
		.ptr = txbuf,
		.end = txbuf + sizeof(txbuf)
	};

	uc_put_hdr(&uc, cmd);
	uc_put_str(&uc, ATTR_NAME, basename(name));
	uc_put_end(&uc);

	int txlen = uc.ptr - uc.brk;

	if(ffd < 0) {
		send_simple(sfd, uc.brk, uc.ptr - uc.brk);
	} else {
		char ancillary[32];
		int anlen = put_cmsg_fd(ancillary, sizeof(ancillary), ffd);

		send_with_anc(sfd, txbuf, txlen, ancillary, anlen);
	}

	recv_reply(sfd, nlen);
}

static int got_args(CTX)
{
	return (ctx->argi < ctx->argc);
}

static int use_opt(CTX, int opt)
{
	if(!(ctx->opts & opt))
		return 0;

	ctx->opts &= ~opt;

	return 1;
}

static char* single_arg(CTX)
{
	if(!got_args(ctx))
		fail("too few arguments", NULL, 0);

	char* arg = ctx->argv[ctx->argi++];

	if(got_args(ctx))
		fail("too many arguments", NULL, 0);
	if(ctx->opts)
		fail("extra options", NULL, 0);

	return arg;
}

static void mount_file(CTX)
{
	char* name = single_arg(ctx);
	int fd = open_rw_or_ro(name);

	cmd_name_fd(CMD_MOUNT_FD, name, fd);
}

static void mount_dev(CTX)
{
	cmd_name_fd(CMD_MOUNT, single_arg(ctx), -1);
}

static void umount(CTX)
{
	cmd_name_fd(CMD_UMOUNT, single_arg(ctx), -1);
}

static void grab_dev(CTX)
{
	cmd_name_fd(CMD_GRAB, single_arg(ctx), -1);
}

static void release(CTX)
{
	cmd_name_fd(CMD_RELEASE, single_arg(ctx), -1);
}

static int cmp(const void* a, const void* b, long _)
{
	char* sa = *((char**)a);
	char* sb = *((char**)b);
	return strcmp(sa, sb);
}

static char** index_devs(struct heap* hp, char* dss, char* dse, int nd)
{
	char** idx = halloc(hp, (nd+1)*sizeof(char*));
	int i = 0;

	for(char* p = dss; p < dse; p += strlen(p) + 1)
		idx[i++] = p;

	idx[i] = NULL;

	return idx;
}

static void foreach_devs(struct heap* hp, void (*call)(struct heap* hp, char**))
{
	int fd, rd;
	char* scb = "/sys/class/block";
	char buf[1024];
	int ndevs = 0;

	if((fd = sys_open(scb, O_DIRECTORY)) < 0)
		fail(NULL, scb, fd);

	char* dss = hp->ptr; /* dev strings start */

	while((rd = sys_getdents(fd, buf, sizeof(buf))) > 0) {
		char* p = buf;
		char* e = buf + rd;

		while(p < e) {
			struct dirent* de = (struct dirent*) p;
			p += de->reclen;

			char* name = de->name;

			if(dotddot(name))
				continue;

			int len = strlen(name);
			char* dn = halloc(hp, len + 1);
			memcpy(dn, name, len + 1);

			ndevs++;
		}
	}

	char* dse = hp->ptr; /* dev strings end */

	char** idx = index_devs(hp, dss, dse, ndevs);

	qsort(idx, ndevs, sizeof(char*), cmp, 0);

	return call(hp, idx);
}

static int check_dev(char* name)
{
	struct stat st;
	int ret;

	if(!strncmp(name, "loop", 4))
		return -1;

	FMTBUF(p, e, path, strlen(name) + 10);
	p = fmtstr(p, e, "/dev/");
	p = fmtstr(p, e, name);
	FMTEND(p, e);

	if((ret = sys_stat(path, &st)) < 0)
		return -1;

	if(major(st.rdev) == LOOP_MAJOR)
		return -1;

	if(st.mode & S_ISVTX)
		return -1;

	return 0;
}

static int isdigit(int c)
{
	return (c >= '0' && c <= '9');
}

/* In most cases when block devices with partitions are involved,
   the users should only be interested in the partitions and not
   the whole devices. To make output somewhat more clean, let's
   filter out the base devices.

   This is a simple string comparison logic, no point in going
   through /sys for this. Some false positives are ok. Also this
   only affects the listing, attempts to mount or grab skipped
   devices may still succeed. */

static int dev_with_parts(char* dev, char* part)
{
	if(!part)
		return 0;

	int dlen = strlen(dev);
	int plen = strlen(part);

	if(dlen < 2)
		return 0;
	if(plen <= dlen)
		return 0;

	if(strncmp(dev, part, dlen))
		return 0;

	char* p = part + dlen;

	if(isdigit(dev[dlen-1]) && *p == 'p')
		p++;

	for(; *p; p++)
		if(!isdigit(*p))
			return 0;

	return 1;
}

static void dump_list(struct heap* hp, char** devs)
{
	int first = 1;
	struct bufout bo = {
		.fd = STDOUT,
		.len = 1024,
		.ptr = 0,
		.buf = halloc(hp, 1024)
	};

	for(char** p = devs; *p; p++) {
		char* name = *p;

		if(dev_with_parts(p[0], p[1]))
			continue;
		if(check_dev(name))
			continue;

		if(first)
			first = 0;
		else
			bufout(&bo, " ", 1);

		bufout(&bo, name, strlen(name));
	}

	if(!first)
		bufout(&bo, "\n", 1);

	bufoutflush(&bo);
}

static void list_devs(CTX)
{
	struct heap hp;

	hinit(&hp, PAGE);

	foreach_devs(&hp, dump_list);
}

static void prep_opts(CTX, int argc, char** argv)
{
	int i = 1, opts = 0;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	ctx->argc = argc;
	ctx->argv = argv;
	ctx->argi = i;
	ctx->opts = opts;
}

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;

	prep_opts(ctx, argc, argv);

	if(use_opt(ctx, OPT_g))
		grab_dev(ctx);
	else if(use_opt(ctx, OPT_r))
		release(ctx);
	else if(use_opt(ctx, OPT_u))
		umount(ctx);
	else if(use_opt(ctx, OPT_f))
		mount_file(ctx);
	else if(got_args(ctx))
		mount_dev(ctx);
	else
		list_devs(ctx);

	return 0;
}
