#include <sys/file.h>
#include <sys/fprop.h>
#include <sys/ppoll.h>
#include <sys/signal.h>
#include <sys/dents.h>
#include <sys/mman.h>
#include <sys/sched.h>
#include <sys/inotify.h>

#include <errtag.h>
#include <format.h>
#include <string.h>
#include <util.h>

#include "common.h"

ERRTAG("sockmod");

struct fbuf {
	char* ptr;
	char* end;
};

struct line {
	char* ptr;
	char* end;
};

struct top {
	int ino;
	int dfd;
	int alarm;

	char* brk;
	char* ptr;
	char* end;

	struct fbuf config;
	struct fbuf groups;
};

struct access {
	int uid;
	int gid;
	uint mode;
};

#define CTX struct top* ctx __unused

static void set_alarm(CTX)
{
	int ret;
	struct itimerval itv = {
		.interval = { 0, 0 },
		.value = { 5, 0 }
	};

	if(ctx->alarm)
		return;

	if((ret = sys_setitimer(ITIMER_REAL, &itv, NULL)) < 0)
		fail("setitimer", NULL, ret);

	ctx->alarm = 1;
}

static void init_heap(CTX)
{
	void* brk = sys_brk(0);

	if(mmap_error(brk))
		fail("cannot allocate memory", NULL, 0);

	ctx->brk = brk;
	ctx->ptr = brk;
	ctx->end = brk;
}

static void* heap_alloc(CTX, long size)
{
	if(!ctx->brk)
		init_heap(ctx);

	void* ptr = ctx->ptr;
	void* req = ptr + size;
	void* end = ctx->end;

	if(req > end && mmap_error(end = sys_brk(req)))
		fail("cannot allocate memory", NULL, 0);

	ctx->end = end;
	ctx->ptr = req;

	return ptr;
}

static void read_file(CTX, struct fbuf* fb, const char* name)
{
	int fd, ret;
	long rd;
	struct stat st;

	if(fb->ptr)
		return;

	if((fd = sys_open(name, O_RDONLY)) < 0)
		fail(NULL, name, fd);
	if((ret = sys_fstat(fd, &st)) < 0)
		fail("stat", name, ret);

	char* buf = heap_alloc(ctx, st.size + 1);

	if((rd = sys_read(fd, buf, st.size)) < 0)
		fail("read", name, rd);
	if(rd < st.size)
		fail("incomplete read from", name, 0);

	buf[st.size] = '\0';

	fb->ptr = buf;
	fb->end = buf + st.size;

	sys_close(fd);
	set_alarm(ctx);
}

static char* eol(struct fbuf* fb, char* ls)
{
	if(ls < fb->ptr)
		return NULL;
	if(ls >= fb->end)
		return NULL;

	return strecbrk(ls, fb->end, '\n');
}

static int isspace(int c)
{
	return (c == ' ' || c == '\t');
}

static int find_entry(CTX, struct fbuf* fb, char* key, struct line* ln)
{
	char *ls, *le, *sep;
	int klen = strlen(key);

	for(ls = fb->ptr; (le = eol(fb, ls)); ls = le + 1) {
		if(ls >= le || *ls == '#')
			continue;
		if((sep = strecbrk(ls, le, ':')) >= le)
			continue;
		if(sep - ls != klen)
			continue;
		if(strncmp(ls, key, klen))
			continue;

		sep++; /* skip : */

		ln->ptr = sep;
		ln->end = le;

		return 0;
	}

	return -1;
}

static long trim_len(struct line* ln)
{
	char* ls = ln->ptr;
	char* le = ln->end;

	while(ls < le && isspace(*ls))
		ls++;

	ln->ptr = ls;

	return le - ls;
}

static void strcpy0(char* dst, char* src, int len)
{
	memcpy(dst, src, len);
	dst[len] = '\0';
}

static int group2gid(CTX, char* grp)
{
	struct line ln;
	struct fbuf* groups = &ctx->groups;
	int gid;
	char* z;

	if((z = parseint(grp, &gid)) && !*z)
		return gid;

	read_file(ctx, groups, HERE "/etc/group");

	if(find_entry(ctx, groups, grp, &ln)) {
		warn("unknown group", grp, 0);
		return -1;
	}

	char* p = ln.ptr;
	char* e = ln.end;

	if((p = strecbrk(p, e, ':')) >= e)
		return -1;
	if(!(p = parseint(p + 1, &gid)))
		return -1;
	if(*p && *p != ':' && *p != '\n')
		return -1;

	return gid;
}

static int lookup(CTX, char* name, struct access* ac)
{
	struct fbuf* config = &ctx->config;
	struct line ln;
	int gid;

	read_file(ctx, config, SOCKCFG);

	if(find_entry(ctx, config, name, &ln))
		return -1;

	int len = trim_len(&ln);
	char grp[len+1];
	strcpy0(grp, ln.ptr, len);

	ac->uid = -1;

	if(!strcmp(grp, "*")) {
		ac->gid = -1;
		ac->mode = 0666;
	} else if((gid = group2gid(ctx, grp)) < 0) {
		return -1;
	} else {
		ac->gid = gid;
		ac->mode = 0660;
	};

	return 0;
}

static void chown(CTX, char* name, int uid, int gid)
{
	int ret;
	int flags = AT_SYMLINK_NOFOLLOW;

	if((ret = sys_fchownat(ctx->dfd, name, uid, gid, flags)) < 0)
		warn("chown", name, ret);
}

static void chmod(CTX, char* name, int mode)
{
	int ret;
	int flags = AT_SYMLINK_NOFOLLOW;

	if((ret = sys_fchmodat(ctx->dfd, name, mode, flags)) < 0)
		warn("chmod", name, ret);
}

/* Sockets do not handle fchmod well, so this should be stat()/chmod()
   instead of open()/fstat()/fchmod(). */

static void check(CTX, char* name)
{
	struct stat st;
	int flags = AT_NO_AUTOMOUNT | AT_SYMLINK_NOFOLLOW;
	int ret;

	if((ret = sys_fstatat(ctx->dfd, name, &st, flags)) < 0)
		return;
	if((st.mode & S_IFMT) != S_IFSOCK)
		return;

	struct access ac;

	if(lookup(ctx, name, &ac) < 0)
		return;

	int need_uid = ac.uid != -1 && st.uid != ac.uid;
	int need_gid = ac.gid != -1 && st.gid != ac.gid;

	if(need_uid || need_gid)
		chown(ctx, name, ac.uid, ac.gid);
	if((st.mode & 0777) != ac.mode)
		chmod(ctx, name, ac.mode);
}

static void sighandler(int sig)
{
	(void)sig;
	/* do nothing */
}

static void setup_signals(void)
{
	struct sigaction sa = {
		.handler = sighandler,
		.flags = SA_RESTORER,
		.restorer = sigreturn
	};

	sys_sigaction(SIGALRM, &sa, NULL);
}

static void start_inotify(CTX, char* dir)
{
	int fd, wd;

	if((fd = sys_inotify_init()) < 0)
		fail("inotify-start", NULL, fd);

	if((wd = sys_inotify_add_watch(fd, dir, IN_CREATE)) < 0)
		fail("inotify-watch", dir, wd);

	ctx->ino = fd;
}

static void scan_directory(CTX, char* dir)
{
	int fd, rd;
	char buf[1024];

	if((fd = sys_open(dir, O_DIRECTORY)) < 0)
		fail(NULL, dir, fd);

	ctx->dfd = fd;

	while((rd = sys_getdents(fd, buf, sizeof(buf))) > 0) {
		char* p = buf;
		char* e = buf + rd;

		while(p < e) {
			struct dirent* de = (struct dirent*) p;

			p += de->reclen;

			char* name = de->name;

			if(dotddot(name))
				continue;

			check(ctx, name);
		}
	}
}

static int wait_inotify(CTX)
{
	char buf[1024];
	int rd, fd = ctx->ino;

	if((rd = sys_read(fd, buf, sizeof(buf))) >= 0)
		;
	else if(rd == -EINTR)
		return rd;
	else
		fail("inotify", NULL, rd);

	void* p = buf;
	void* e = buf + rd;

	while(p < e) {
		struct inotify_event* evt = p;

		p += sizeof(*evt) + evt->len;

		check(ctx, evt->name);
	}

	return rd;
}

static void drop_file_bufs(CTX)
{
	void* brk = ctx->brk;

	brk = sys_brk(brk);

	if(mmap_error(brk))
		fail("cannot shrink heap", NULL, 0);

	ctx->ptr = brk;
	ctx->end = brk;

	memzero(&ctx->config, sizeof(ctx->config));
	memzero(&ctx->groups, sizeof(ctx->groups));

	ctx->alarm = 0;
}

int main(int argc, char** argv)
{
	(void)argv;
	struct top context, *ctx = &context;
	char* dir = RUN_CTRL;

	if(argc > 1)
		fail("too many arguments", NULL, 0);

	memzero(ctx, sizeof(*ctx));

	setup_signals();
	start_inotify(ctx, dir);
	scan_directory(ctx, dir);

	while(1) {
		if(wait_inotify(ctx) >= 0)
			continue;
		drop_file_bufs(ctx);
	}

	return 0; /* never reached */
}
