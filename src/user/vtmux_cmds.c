#include <bits/socket.h>
#include <bits/socket/unix.h>
#include <bits/errno.h>
#include <sys/recv.h>
#include <sys/socket.h>
#include <sys/listen.h>
#include <sys/bind.h>
#include <sys/write.h>
#include <sys/close.h>

#include <format.h>
#include <string.h>
#include <fail.h>

#include "vtmux.h"

static void reply(int fd, int errno, char* msg)
{
	char repbuf[256];

	char* p = repbuf;
	char* e = repbuf + sizeof(repbuf);

	if(errno)
		p = fmtint(p, e, -errno);
	if(errno && msg)
		p = fmtstr(p, e, " ");
	if(msg)
		p = fmtstr(p, e, msg);

	syswrite(fd, repbuf, p - repbuf);
}

static void cmd_switch(int fd, char* arg)
{
	int vt;
	char* p;
	long ret;

	if(!(p = parseint(arg, &vt)) || *p)
		return reply(fd, -EINVAL, "Bad request");

	if((ret = switchto(vt)))
		return reply(fd, ret, NULL);

	return reply(fd, 0, NULL);
}

static void cmd_spawn(int fd, char* arg)
{
	long ret = spawn(arg);

	return reply(fd, ret, NULL);
}

static void cmd_open(int fd, char* arg)
{
	return reply(fd, -ENOSYS, "Not implemented");
}

static void handlecmd(int ci, int fd, char* cmd)
{
	char* arg = cmd + 1;

	if(*cmd == '@')
		return cmd_open(fd, arg);

	/* non-greeter clients cannot spawn sessions */
	if(ci) goto out;

	if(*cmd == '=')
		return cmd_switch(fd, arg);
	if(*cmd == '+')
		return cmd_spawn(fd, arg);
out:
	return reply(fd, -EINVAL, "Invalid command");
}

void handlectl(int ci, int fd)
{
	char cmdbuf[256];
	int cmdsize = sizeof(cmdbuf) - 1;
	int rd;

	while((rd = sysrecv(fd, cmdbuf, cmdsize, MSG_DONTWAIT)) > 0) {
		cmdbuf[rd] = '\0';
		handlecmd(ci, fd, cmdbuf);
	} if(rd < 0 && rd != -EAGAIN) {
		warn("recv", NULL, rd);
	}
}
