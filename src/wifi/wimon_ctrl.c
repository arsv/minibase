#include <bits/socket.h>
#include <bits/socket/unix.h>
#include <sys/accept.h>
#include <sys/alarm.h>
#include <sys/bind.h>
#include <sys/close.h>
#include <sys/getsockopt.h>
#include <sys/getuid.h>
#include <sys/kill.h>
#include <sys/listen.h>
#include <sys/read.h>
#include <sys/socket.h>
#include <sys/write.h>
#include <sys/unlink.h>

#include <string.h>
#include <format.h>
#include <fail.h>
#include <util.h>

#include "config.h"
#include "wimon.h"

#define TIMEOUT 1

static void parsecmd(int fd, char* cmd)
{
	char blah[] = "here\n";

	writeall(fd, blah, sizeof(blah));
}

static int checkuser(int fd)
{
	struct ucred cred;
	int credlen = sizeof(cred);

	if(sysgetsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &credlen))
		return -1;

	if(cred.uid != sysgetuid())
		return -1;

	return 0;
}

static void readcmd(int fd)
{
	int rb;
	char cbuf[NAMELEN+10];

	if((rb = sysread(fd, cbuf, NAMELEN+1)) < 0)
		return warn("recvmsg", NULL, rb);
	if(rb >= NAMELEN)
		return warn("recvmsg", "message too long", 0);
	cbuf[rb] = '\0';

	parsecmd(fd, cbuf);
}

void accept_ctrl(int sfd)
{
	int cfd;
	int gotcmd = 0;
	struct sockaddr addr;
	int addr_len = sizeof(addr);

	while((cfd = sysaccept(sfd, &addr, &addr_len)) > 0) {
		int nonroot = checkuser(cfd);

		if(nonroot) {
			const char* denied = "Access denied\n";
			syswrite(cfd, denied, strlen(denied));
		} else {
			gotcmd = 1;
			sysalarm(TIMEOUT);
			readcmd(cfd);
		}

		sysclose(cfd);

	} if(gotcmd) {
		/* disable the timer in case it has been set */
		sysalarm(0);
	}
}

void unlink_ctrl(void)
{
	sysunlink(WICTL);
}

void setup_ctrl(void)
{
	int fd;
	struct sockaddr_un addr = {
		.family = AF_UNIX,
		.path = WICTL
	};

	const int flags = SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC;
	if((fd = syssocket(AF_UNIX, flags, 0)) < 0)
		fail("socket", "AF_UNIX", fd);

	long ret;
	char* name = WICTL;

	ctrlfd = fd;

	if((ret = sysbind(fd, &addr, sizeof(addr))) < 0)
		fail("bind", name, ret);
	else if((ret = syslisten(fd, 1)))
		fail("listen", name, ret);
	else
		return;

	ctrlfd = -1;
}
