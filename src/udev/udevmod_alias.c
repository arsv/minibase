#include <sys/file.h>
#include <sys/proc.h>

#include <string.h>
#include <printf.h>
#include <util.h>

#include "udevmod.h"

/* During initial device scan, udevmod will try dozens of modaliases
   in quick succession, most of them invalid. There is no point in
   spawning that many modprobe processes. Instead, we spawn one and
   pipe it aliases to check.

   After the initial scan, this becomes pointless since udev events
   are rare and the ones with modaliases tend to arrive one at a time,
   so we switch to spawning modprobe on each event. */

void open_modprobe(CTX)
{
	int fds[2];
	int ret, pid;

	if((ret = sys_pipe(fds)) < 0)
		fail("pipe", NULL, ret);
	if((pid = sys_fork()) < 0)
		fail("fork", NULL, pid);

	if(pid == 0) {
		char* argv[] = { "/sbin/modprobe", "-qbp", NULL };
		sys_dup2(fds[0], STDIN);
		sys_close(fds[1]);
		ret = sys_execve(*argv, argv, ctx->envp);
		fail("execve", *argv, ret);
	}

	sys_close(fds[0]);

	ctx->pid = pid;
	ctx->fd = fds[1];
}

void stop_modprobe(CTX)
{
	int pid = ctx->pid;
	int ret, status;
	int fd = ctx->fd;

	if(fd < 0) fd = -fd;

	sys_close(fd);

	if((ret = sys_waitpid(pid, &status, 0)) < 0)
		fail("waitpid", NULL, ret);

	ctx->fd = -1;
	ctx->pid = 0;
}

static void run_modprobe(CTX, char* name)
{
	int pid, ret, status;

	if((pid = sys_fork()) < 0) {
		warn("fork", NULL, pid);
		return;
	}

	if(pid == 0) {
		char* argv[] = { "/sbin/modprobe", "-qb", name, NULL };
		ret = sys_execve(*argv, argv, ctx->envp);
		fail("execve", *argv, ret);
	} else {
		if((ret = sys_waitpid(pid, &status, 0)) < 0)
			warn("waitpid", NULL, ret);
	}
}

static void out_modprobe(CTX, char* name)
{
	int fd = ctx->fd;
	int nlen = strlen(name);
	int ret;

	if(fd <= 0) return;

	name[nlen] = '\n';

	ret = sys_write(ctx->fd, name, nlen + 1);

	name[nlen] = '\0';

	if(ret == -EPIPE) ctx->fd = -ctx->fd;
}

void modprobe(CTX, char* name)
{
	if(ctx->pid)
		out_modprobe(ctx, name);
	else
		run_modprobe(ctx, name);
}
