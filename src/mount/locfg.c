#include <bits/ioctl/loop.h>
#include <bits/major.h>

#include <sys/file.h>
#include <sys/fprop.h>
#include <sys/ioctl.h>

#include <format.h>
#include <string.h>
#include <util.h>
#include <main.h>

ERRTAG("locfg");

#define OPTS "di"
#define OPT_d (1<<0)
#define OPT_i (1<<1)

struct top {
	int argc;
	char** argv;
	int argi;
	int opts;
};

#define CTX struct top* ctx

static void set_opts(CTX, int argc, char** argv)
{
	int i = 1;

	ctx->argc = argc;
	ctx->argv = argv;

	if(i < argc && argv[i][0] == '-')
		ctx->opts = argbits(OPTS, argv[i++] + 1);
	else
		ctx->opts = 0;

	ctx->argi = i;
}

static int use_opt(CTX, int opt)
{
	int opts = ctx->opts;

	if(!(opts & opt))
		return 0;

	ctx->opts = opts & ~opt;
	return 1;
}

static char* shift_arg(CTX)
{
	if(ctx->argi >= ctx->argc)
		fail("too few arguments", NULL, 0);

	return ctx->argv[ctx->argi++];
}

static char* prefixed(char* str, char* pref)
{
	int slen = strlen(str);
	int plen = strlen(pref);

	if(plen >= slen)
		return NULL;
	if(strncmp(str, pref, plen))
		return NULL;

	return str + plen;
}

static void shift_idx(CTX, int* val)
{
	char* arg = shift_arg(ctx);
	char* p;

	if((p = prefixed(arg, "/dev/loop")))
		arg = p;
	else if((p = prefixed(arg, "loop")))
		arg = p;

	if(!(p = parseint(arg, val)) || *p)
		fail("bad loop dev specification", NULL, 0);

}

static void shift_u64(CTX, uint64_t* val)
{
	char* arg = shift_arg(ctx);
	char* p;

	if(!(p = parseu64(arg, val)) || *p)
		fail("integer argument required:", arg, 0);
}

static int got_more_arguments(CTX)
{
	return (ctx->argi < ctx->argc);
}

static void no_more_arguments(CTX)
{
	if(ctx->opts)
		fail("extra options", NULL, 0);
	if(got_more_arguments(ctx))
		fail("too many arguments", NULL, 0);
}

static int open_loop_dev(int idx)
{
	int fd, ret;
	struct stat st;

	FMTBUF(p, e, path, 30);
	p = fmtstr(p, e, "/dev/loop");
	p = fmtint(p, e, idx);
	FMTEND(p, e);

	if((fd = sys_open(path, O_RDONLY)) < 0)
		fail(NULL, path, fd);
	if((ret = sys_fstat(fd, &st)) < 0)
		fail("stat", path, ret);
	if((st.mode & S_IFMT) != S_IFBLK)
		fail("not a block device:", path, 0);
	if(major(st.rdev) != LOOP_MAJOR)
		fail("not a loop device:", path, 0);

	return fd;
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

static void ioctli(int fd, int cmd, long arg, char* tag)
{
	int ret;

	if((ret = sys_ioctli(fd, cmd, arg)) < 0)
		fail("ioctl", tag, ret);
}

static void dump_loop_name(int idx)
{
	FMTBUF(p, e, buf, 200);
	p = fmtstr(p, e, "/dev/loop");
	p = fmtint(p, e, idx);
	FMTENL(p, e);

	writeall(STDOUT, buf, p - buf);
}

static void dump_loop_info(int idx, struct loop_info64* info)
{
	FMTBUF(p, e, buf, 200);

	p = fmtstr(p, e, "/dev/loop");
	p = fmtint(p, e, idx);
	p = fmtstr(p, e, ": ");
	p = fmtstr(p, e, (char*)info->file_name);

	if(info->offset || info->sizelimit) {
		p = fmtstr(p, e, " ");
		p = fmtu64(p, e, info->offset);
	} if(info->sizelimit) {
		p = fmtstr(p, e, " ");
		p = fmtu64(p, e, info->sizelimit);
	}

	FMTENL(p, e);

	writeall(STDOUT, buf, p - buf);
}

static void detach(CTX)
{
	int idx;

	shift_idx(ctx, &idx);
	no_more_arguments(ctx);

	int fd = open_loop_dev(idx);

	ioctli(fd, LOOP_CLR_FD, 0, "LOOP_CLR_FD");
}

static int open_unused_loop(int* idx)
{
	int fd, ret;
	char* name = "/dev/loop-control";

	if((fd = sys_open(name, O_RDONLY)) < 0)
		fail(NULL, name, fd);

	if((ret = sys_ioctli(fd, LOOP_CTL_GET_FREE, 0)) < 0)
		fail("ioctl", "LOOP_CTL_GET_FREE", ret);

	sys_close(fd);

	*idx = ret;

	return open_loop_dev(ret);
}

static void set_loop_name(struct loop_info64* info, char* name)
{
	char* base = basename(name);
	int need_len = strlen(base);
	int have_len = sizeof(info->file_name) - 1;

	if(need_len > have_len)
		fail("name too long:", name, 0);

	char* buf = (char*)info->file_name;
	int len = need_len;

	memcpy(buf, base, len);
	buf[len] = '\0';
}

static void attach(CTX)
{
	struct loop_info64 info;
	memzero(&info, sizeof(info));

	char* name = shift_arg(ctx);

	set_loop_name(&info, name);

	if(got_more_arguments(ctx))
		shift_u64(ctx, &info.offset);
	if(got_more_arguments(ctx))
		shift_u64(ctx, &info.sizelimit);

	no_more_arguments(ctx);

	int idx, ret;
	int ffd = open_rw_or_ro(name);
	int lfd = open_unused_loop(&idx);

	ioctli(lfd, LOOP_SET_FD, ffd, "LOOP_SET_FD");

	if((ret = sys_ioctl(lfd, LOOP_SET_STATUS64, &info)) < 0) {
		warn("ioctl", "LOOP_SET_STATUS64", ret);
		ioctli(lfd, LOOP_CLR_FD, 0, "LOOP_CLR_FD");
		_exit(-1);
	}

	dump_loop_name(idx);
}

static void showinfo(CTX)
{
	int idx, ret;

	shift_idx(ctx, &idx);
	no_more_arguments(ctx);

	struct loop_info64 info;
	memzero(&info, sizeof(info));

	int fd = open_loop_dev(idx);

	if((ret = sys_ioctl(fd, LOOP_GET_STATUS64, &info)) >= 0)
		;
	else if(ret == -ENXIO)
		fail("device is not bound", NULL, 0);
	else
		fail("ioctl", "LOOP_GET_STATUS64", ret);

	dump_loop_info(idx, &info);
}

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;

	set_opts(ctx, argc, argv);

	if(use_opt(ctx, OPT_d))
		detach(ctx);
	else if(use_opt(ctx, OPT_i))
		showinfo(ctx);
	else
		attach(ctx);

	return 0;
}
