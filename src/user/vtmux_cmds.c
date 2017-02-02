#include <bits/socket.h>
#include <sys/recv.h>
#include <sys/close.h>
#include <fail.h>
#include "vtmux.h"

char cmdbuf[256];

static void clientcmd(struct vtx* cvt, int fd, char* buf, int len)
{
	
}

static void mastercmd(struct vtx* cvt, int fd, char* buf, int len)
{

}

void handlectl(int ci, int fd)
{
	struct vtx* cvt = &consoles[ci];
	int rd;

	while((rd = sysrecv(fd, cmdbuf, sizeof(cmdbuf)-1, MSG_DONTWAIT)) > 0) {
		cmdbuf[rd] = '\0';

		if(ci == 0)
			mastercmd(cvt, fd, cmdbuf, rd);
		else
			clientcmd(cvt, fd, cmdbuf, rd);

	} if(rd < 0 && rd != -EAGAIN) {
		warn("recv", NULL, rd);
	}
}
