/* Simplified udev event monitor for initrd use */

#include <bits/socket/netlink.h>
#include <sys/file.h>
#include <sys/proc.h>
#include <sys/ppoll.h>
#include <sys/creds.h>
#include <sys/dents.h>
#include <sys/socket.h>
#include <sys/signal.h>
#include <sys/timer.h>
#include <sys/mman.h>

#include <config.h>
#include <string.h>
#include <sigset.h>
#include <format.h>
#include <util.h>
#include <main.h>

ERRTAG("devinit");

#define OPTS "r"
#define OPT_r (1<<0)

struct top {
	char* base;
	char** argv;
	char** envp;
	int failure;

	int devfd;  /* udev socket, in */
	int sigfd;  /* signalfd, in */
	int modfd;  /* modpipe, out */
	int runpid; /* primary child script */
	int modpid; /* modpipe */

	struct pollfd pfds[2];

	char uevent[1024+2];
};

#define CTX struct top* ctx __unused

#define UDEV_MGRP_KERNEL   (1<<0)

static void modprobe(CTX, char* name)
{
	int fd = ctx->modfd;
	int nlen = strlen(name);
	int ret;

	if(fd < 0) /* modpipe is dead */
		return;

	name[nlen] = '\n';

	ret = sys_write(fd, name, nlen + 1);

	name[nlen] = '\0';

	if(ret > 0) /* write successful */
		return;

	ctx->modfd = -fd; /* prevent subsequent writes */

	if(ret == -EPIPE)
		return; /* will be reported by other means */

	warn("write", "modpipe", ret);
}

char* get_val(char* buf, int len, char* key)
{
	char* p = buf;
	char* q;
	char* e = buf + len;

	int klen = strlen(key);

	while(p < e) {
		if(!(q = strchr(p, '=')))
			goto next;
		if(q - p < klen)
			goto next;

		if(!strncmp(p, key, klen))
			return q + 1;

		next: p += strlen(p) + 1;
	}

	return NULL;
}

static void recv_event(CTX)
{
	int rd, fd = ctx->devfd;
	int max = sizeof(ctx->uevent) - 2;
	char* buf = ctx->uevent;
	char* alias;

	if((rd = sys_recv(fd, buf, max, 0)) < 0)
		fail("recv", "udev", rd);

	buf[rd] = '\0';

	if(rd < 4)
		return;
	if(strncmp(buf, "add@", 4))
		return;
	if(!(alias = get_val(buf, rd, "MODALIAS")))
		return;

	modprobe(ctx, alias);
}

static noreturn void exec_with_args(CTX, char* path)
{
	ctx->argv[0] = path;

	int ret = sys_execve(path, ctx->argv, ctx->envp);

	fail(NULL, path, ret);
}

static noreturn void exec_no_args(CTX, char* path)
{
	char* argv[] = { path, NULL };

	int ret = sys_execve(path, argv, ctx->envp);

	fail("execve", path, ret);
}

void open_modpipe(CTX)
{
	int fds[2];
	int ret, pid;

	if((ret = sys_pipe2(fds, O_CLOEXEC)) < 0)
		fail("pipe", NULL, ret);
	if((pid = sys_fork()) < 0)
		fail("fork", NULL, pid);

	if(pid == 0) {
		sys_dup2(fds[0], STDIN);
		sys_close(fds[1]);
		exec_no_args(ctx, INIT_ETC "/modpipe");
	}

	sys_close(fds[0]);

	ctx->modpid = pid;
	ctx->modfd = fds[1];
}

static void open_udev(CTX)
{
	int fd, ret;
	int pid = sys_getpid();

	int domain = PF_NETLINK;
	int type = SOCK_DGRAM;
	int proto = NETLINK_KOBJECT_UEVENT;

	if((fd = sys_socket(domain, type, proto)) < 0)
		fail("socket", "udev", fd);

	struct sockaddr_nl addr = {
		.family = AF_NETLINK,
		.pid = pid,
		.groups = UDEV_MGRP_KERNEL
	};

	if((ret = sys_bind(fd, &addr, sizeof(addr))) < 0)
		fail("bind", "udev", ret);

	ctx->devfd = fd;
}

static void pick_modalias(CTX, int at)
{
	int fd, rd;
	char buf[100];
	int len = sizeof(buf) - 1;

	if((fd = sys_openat(at, "modalias", O_RDONLY)) < 0)
		return;

	if((rd = sys_read(fd, buf, len)) > 0) {
		if(buf[rd-1] == '\n')
			buf[rd-1] = '\0';
		else
			buf[rd] = '\0';

		modprobe(ctx, buf);
	}

	sys_close(fd);
}

static void scan_dir(CTX, int at, char* name)
{
	int len = 1024;
	char buf[len];
	int fd, rd;

	if((fd = sys_openat(at, name, O_DIRECTORY)) < 0)
		return;

	while((rd = sys_getdents(fd, buf, len)) > 0) {
		char* ptr = buf;
		char* end = buf + rd;

		while(ptr < end) {
			struct dirent* de = (struct dirent*) ptr;

			if(!de->reclen)
				break;

			ptr += de->reclen;

			if(dotddot(de->name))
				continue;
			if(de->type == DT_DIR)
				scan_dir(ctx, fd, de->name);
			else if(de->type != DT_REG)
				continue;
			if(strcmp(de->name, "modalias"))
				continue;

			pick_modalias(ctx, fd);

			goto done;
		}
	}
done:
	sys_close(fd);
}

static void scan_devices(CTX)
{
	scan_dir(ctx, AT_FDCWD, "/sys/devices");
}

static void open_signals(CTX)
{
	int fd, ret;
	int flags = SFD_CLOEXEC | SFD_NONBLOCK;
	struct sigset mask;

	sigemptyset(&mask);
	sigaddset(&mask, SIGCHLD);

	if((fd = sys_signalfd(-1, &mask, flags)) < 0)
		fail("signalfd", NULL, fd);

	sigaddset(&mask, SIGPIPE);

	if((ret = sys_sigprocmask(SIG_BLOCK, &mask, NULL)) < 0)
		fail("sigprocmask", NULL, ret);

	ctx->sigfd = fd;
}

static void start_script(CTX)
{
	int pid;

	if((pid = sys_fork()) < 0)
		fail("fork", NULL, pid);

	if(pid == 0)
		exec_with_args(ctx, INIT_ETC "/setup");

	ctx->runpid = pid;
}

static void terminate(CTX)
{
	if(ctx->failure)
		exec_no_args(ctx, INIT_ETC "/failure");
	else
		exec_with_args(ctx, INIT_ETC "/success");
}

static void kill_check(CTX, int pid, char* tag)
{
	int ret;

	if((ret = sys_kill(pid, SIGTERM)) < 0) {
		warn("kill", tag, ret);
		return terminate(ctx);
	}

	if((sys_alarm(5)) < 0)
		warn("alarm", NULL, ret);
}

static void trigger_failure(CTX)
{
	ctx->failure = -1;
}

static void handle_script_exit(CTX, int status)
{
	int modpid = ctx->modpid;

	ctx->runpid = -1;

	if(status)
		trigger_failure(ctx);

	if(modpid <= 0)
		return terminate(ctx);

	kill_check(ctx, modpid, "modpipe");
}

static void handle_modpipe_exit(CTX)
{
	int runpid = ctx->runpid;

	ctx->modpid = -1;

	if(runpid <= 0)
		return terminate(ctx);

	warn("modpipe died", NULL, 0);
	trigger_failure(ctx);

	kill_check(ctx, runpid, "script");
}

static void check_children(CTX)
{
	int pid, status;

	if((pid = sys_waitpid(-1, &status, WNOHANG)) <= 0)
		return;

	if(pid == ctx->runpid)
		return handle_script_exit(ctx, status);
	if(pid == ctx->modpid)
		return handle_modpipe_exit(ctx);
}

static void recv_signal(CTX)
{
	int fd = ctx->sigfd;
	struct siginfo si;
	int rd;

	memzero(&si, sizeof(si));

	if((rd = sys_read(fd, &si, sizeof(si))) < 0)
		return;

	if(si.signo != SIGCHLD)
		return;

	check_children(ctx);
}

static void prep_pollfds(CTX)
{
	ctx->pfds[0].fd = ctx->devfd;
	ctx->pfds[0].events = POLLIN;
	ctx->pfds[1].fd = ctx->sigfd;
	ctx->pfds[1].events = POLLIN;
}

static void poll(CTX)
{
	int ret;
	struct pollfd* pfds = ctx->pfds;

	if((ret = sys_ppoll(pfds, 2, NULL, NULL)) < 0)
		fail("ppoll", NULL, ret);

	if(pfds[0].revents & POLLIN)
		recv_event(ctx);
	if(pfds[1].revents & POLLIN)
		recv_signal(ctx);

	if(pfds[0].revents & ~POLLIN)
		pfds[0].fd = -1;
	if(pfds[1].revents & ~POLLIN)
		pfds[1].fd = -1;
}

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;

	memzero(ctx, sizeof(*ctx));

	ctx->argv = argv;
	ctx->envp = argv + argc + 1;

	open_udev(ctx);
	open_signals(ctx);
	open_modpipe(ctx);
	scan_devices(ctx);
	prep_pollfds(ctx);
	start_script(ctx);

	while(1) poll(ctx);
}
