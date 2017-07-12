#include <bits/socket.h>
#include <bits/socket/unix.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/kill.h>
#include <sys/itimer.h>

#include <nlusctl.h>
#include <string.h>
#include <format.h>
#include <alloca.h>
#include <util.h>

#include "svmon.h"

void close_conn(struct conn* cn)
{
	sys_close(cn->fd);
	memzero(cn, sizeof(*cn));
	gg.pollset = 0;
}

void handle_conn(struct conn* cn)
{
	int ret, fd = cn->fd;

	char rxbuf[500];
	struct urbuf ur = {
		.buf = rxbuf,
		.mptr = rxbuf,
		.rptr = rxbuf,
		.end = rxbuf + sizeof(rxbuf)
	};
	struct itimerval old, itv = {
		.interval = { 0, 0 },
		.value = { 1, 0 }
	};

	sys_setitimer(0, &itv, &old);

	while(1) {
		if((ret = uc_recv(fd, &ur, 0)) < 0)
			break;

		dispatch_cmd(cn, ur.msg);

		if(ur.mptr >= ur.rptr)
			break;
	}

	if(ret != -EBADF && ret != -EAGAIN)
		close_conn(cn);

	sys_setitimer(0, &old, NULL);
}

void accept_ctrl(int sfd)
{
	int cfd;
	struct sockaddr addr;
	int addr_len = sizeof(addr);
	struct conn *cn;

	while((cfd = sys_accept(sfd, &addr, &addr_len)) > 0)
		if((cn = grab_conn_slot()))
			cn->fd = cfd;
		else
			sys_close(cfd);

	gg.pollset = 0;
}

void setup_ctrl(void)
{
	int fd;
	struct sockaddr_un addr = {
		.family = AF_UNIX,
		.path = SVCTL
	};
	const int flags = SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC;

	if((fd = sys_socket(AF_UNIX, flags, 0)) < 0)
		return report("socket", "AF_UNIX", fd);

	long ret;
	char* name = SVCTL;

	gg.ctrlfd = fd;
	gg.pollset = 0;

	if((ret = sys_bind(fd, &addr, sizeof(addr))) < 0)
		report("bind", name, ret);
	else if((ret = sys_listen(fd, 1)))
		report("listen", name, ret);
	else
		return;

	gg.ctrlfd = -1;
	sys_close(fd);
}
