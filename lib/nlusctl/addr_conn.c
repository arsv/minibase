#include <bits/socket/unix.h>
#include <sys/socket.h>
#include <sys/fpath.h>

#include <nlusctl.h>
#include "addr.h"

int uc_connect(int fd, const char* path)
{
	int ret;
	struct sockaddr_un addr;

	if((ret = uc_address(&addr, path)) < 0)
		return ret;

	return sys_connect(fd, &addr, sizeof(addr));
}
