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

char cmdbuf[256];
char repbuf[256];

static void reply(int fd, int errno, char* msg)
{
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
	long ret = spawn_client(arg);

	return reply(fd, ret, NULL);
}

static void cmd_open(int fd, char* arg)
{
	return reply(fd, -ENOSYS, "Not implemented");
}

static void handlecmd(int ci, int fd, char* cmd)
{
	if(!*cmd)
		goto invalid;

	char* arg = cmd + 1;

	if(ci > 0)
		goto nonpriv;
	if(*cmd == '=')
		return cmd_switch(fd, arg);
	if(*cmd == '+')
		return cmd_spawn(fd, arg);
	if(ci < 0)
		goto invalid;
nonpriv:
	if(*cmd == '@')
		return cmd_open(fd, arg);
invalid:
	return reply(fd, -EINVAL, "Invalid command");
}

void handlectl(int ci, int fd)
{
	int rd;

	while((rd = sysrecv(fd, cmdbuf, sizeof(cmdbuf)-1, MSG_DONTWAIT)) > 0) {
		cmdbuf[rd] = '\0';
		handlecmd(ci, fd, cmdbuf);
	} if(rd < 0 && rd != -EAGAIN) {
		warn("recv", NULL, rd);
	}
}
