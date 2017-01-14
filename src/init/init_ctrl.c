#include <bits/socket.h>
#include <bits/socket/unix.h>
#include <bits/socket.h>
#include <sys/accept.h>
#include <sys/bind.h>
#include <sys/listen.h>
#include <sys/socket.h>
#include <sys/alarm.h>
#include <sys/read.h>
#include <sys/close.h>
#include <sys/write.h>
#include <sys/getsockopt.h>
#include <sys/getuid.h>

#include <string.h>

#include "init.h"

void setinitctl(void)
{
	int fd;
	struct sockaddr_un addr = {
		.family = AF_UNIX,
		.path = INITCTL
	};

	/* This way readable "@initctl" can be used for reporting below,
	   and config.h looks better too. */
	if(addr.path[0] == '@')
		addr.path[0] = '\0';

	/* we're not going to block for connections, just accept whatever
	   is already there; so it's SOCK_NONBLOCK */
	const int flags = SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC;
	if((fd = syssocket(AF_UNIX, flags, 0)) < 0)
		return report("socket", "AF_UNIX", fd);

	long ret;
	char* name = INITCTL;

	gg.ctlfd = fd;

	if((ret = sysbind(fd, &addr, sizeof(addr))) < 0)
		report("bind", name, ret);
	else if((ret = syslisten(fd, 1)))
		report("listen", name, ret);
	else
		return;

	sysclose(fd);
	gg.ctlfd = -1;
}

static int checkuser(int fd)
{
	struct ucred cred;
	int credlen = sizeof(cred);

	if(sysgetsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &credlen))
		return -1;

	if(cred.uid != gg.uid)
		return -1;

	return 0;
}

static void readcmd(int fd)
{
	int rb;
	char cbuf[NAMELEN+10];

	if((rb = sysread(fd, cbuf, NAMELEN+1)) < 0)
		return report("recvmsg", NULL, rb);
	if(rb >= NAMELEN)
		return report("recvmsg", "message too long", 0);
	cbuf[rb] = '\0';

	gg.outfd = fd;
	parsecmd(cbuf);
	gg.outfd = STDERR;
}

void acceptctl(void)
{
	int fd;
	int gotcmd = 0;
	struct sockaddr addr;
	int addr_len = sizeof(addr);

	while((fd = sysaccept(gg.ctlfd, &addr, &addr_len)) > 0) {
		int nonroot = checkuser(fd);

		if(nonroot) {
			const char* denied = "Access denied\n";
			syswrite(fd, denied, strlen(denied));
		} else {
			gotcmd = 1;
			sysalarm(INITCTL_TIMEOUT);
			readcmd(fd);
		}

		sysclose(fd);

	} if(gotcmd) {
		/* disable the timer in case it has been set */
		sysalarm(0);
	}
}
