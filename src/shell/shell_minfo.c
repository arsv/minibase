#include <sys/file.h>
#include <sys/mount.h>
#include <string.h>

#include <format.h>
#include <output.h>

#include "shell.h"

/* A couple of commands to examine current mounts.

   This is not strictly necessary, something like `read /proc/self/mounts`
   would suffice is most cases, but it's a nice in QoL sense and given the
   likely use of this as a recovery shell, it might come handy.

   All this code does is just going through /proc/self/mountinfo
   and formatting the lines a bit to make the more readable.

   There are separate commands for virtual and real filesystems, just to
   avoid VFS noise when all you need is the actual mounted devices. */

#define MINSIZE 1024
#define OUTSIZE 1024

#define MODE_ALL    0
#define MODE_VIRT   1
#define MODE_REAL   2

struct minfocontext {
	int mode;
	char* minbuf;
	char* outbuf;
	struct bufout bo;
};

struct minfo {
	char* devstr;
	char* mountpoint;
	char* device;
	char* fstype;
	char* options;
};

#define CTX struct minfocontext* ctx

static char* findline(char* p, char* e)
{
	while(p < e)
		if(*p == '\n')
			return p;
		else
			p++;
	return NULL;
}

static void report_mount(CTX, struct minfo* mi)
{
	char* device = mi->device;
	char* mp = mi->mountpoint;
	char* fs = mi->fstype;
	char* opts = mi->options;

	int max = 1024;
	char* buf = alloca(max);
	char* p = buf;
	char* e = buf + max - 1;

	p = fmtstr(p, e, " ");
	p = fmtstr(p, e, mp);
	p = fmtstr(p, e, " =");

	if(fs) {
		p = fmtstr(p, e, " ");
		p = fmtstr(p, e, fs);
	} else {
		fs = "???";
	}

	if(device && strcmp(fs, device)) {
		p = fmtstr(p, e, ":");
		p = fmtstr(p, e, device);
	}

	if(opts) {
		p = fmtstr(p, e, " (");
		p = fmtstr(p, e, opts);
		p = fmtstr(p, e, ")");
	}

	*p++ = '\n';

	bufout(&ctx->bo, buf, p - buf);
}

static void scan_check_mount(CTX, struct minfo* mi)
{
	char* devstr = mi->devstr;

	if(!devstr)
		return;

	int virtual = (devstr[0] == '0' && devstr[1] == ':');
	int mode = ctx->mode;

	if((mode == MODE_REAL) && virtual)
		return;
	if((mode == MODE_VIRT) && !virtual)
		return;

	report_mount(ctx, mi);
}

static void set_minfo(struct minfo* mi, int i, char* p)
{
	if(i == 2)
		mi->devstr = p;
	else if(i == 4)
		mi->mountpoint = p;
	else if(i == 8)
		mi->fstype = p;
	else if(i == 9)
		mi->device = p;
	else if(i == 10)
		mi->options = p;
}

static void scan_mount_line(CTX, char* ls, char* le)
{
	struct minfo fields, *mi = &fields;
	int i, n = 10;

	char* p = ls;
	char* q;

	memzero(mi, sizeof(*mi));

	for(i = 0; i < n; i++) {
		if(!*(q = strcbrk(p, ' ')))
			break;

		set_minfo(mi, i, p);

		*q = '\0';
		p = q + 1;
	};

	if(p < le)
		set_minfo(mi, i, p);

	scan_check_mount(ctx, mi);
}

static void scan_mountinfo(CTX)
{
	char* name = "/proc/self/mountinfo";
	int fd, rd, off = 0;
	char* buf = ctx->minbuf;
	int buflen = MINSIZE;

	if((fd = sys_open(name, O_RDONLY)) < 0)
		return repl(NULL, name, fd);

	while((rd = sys_read(fd, buf + off, buflen - off)) > 0) {
		char* p = buf;
		char* e = buf + rd;
		char* q;

		while((q = findline(p, e))) {
			*q = '\0';
			scan_mount_line(ctx, p, q);
			p = q + 1;
		} if(p < e) {
			off = e - p;
			memcpy(buf, p, off);
		}

		if(rd < buflen - off)
			break; /* last chunk, don't bother reading further */
	}

	check_close(fd);
}

static void list_mounts(int mode)
{
	struct minfocontext context, *ctx = &context;

	if(extra_arguments())
		return;

	memzero(ctx, sizeof(*ctx));

	ctx->mode = mode;
	ctx->minbuf = heap_alloc(MINSIZE);
	ctx->outbuf = heap_alloc(OUTSIZE);

	bufoutset(&ctx->bo, STDOUT, ctx->outbuf, OUTSIZE);

	scan_mountinfo(ctx);

	bufoutflush(&ctx->bo);
}

void cmd_mounts(void)
{
	list_mounts(MODE_ALL);
}

void cmd_rfs(void)
{
	list_mounts(MODE_REAL);
}

void cmd_vfs(void)
{
	list_mounts(MODE_VIRT);
}
