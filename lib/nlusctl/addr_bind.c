#include <bits/socket/unix.h>
#include <sys/socket.h>
#include <sys/fpath.h>

#include <nlusctl.h>
#include "addr.h"

/* Bind a listening socket, removing stale one if present.
   This is to allow a service to restart even if the first
   instance failed to clean up the socket it created.

   Now I would much rather prefer not having to write this function, but it
   looks like this is the lesser evil among what I can do with current Linux
   syscalls. It is racy, but the race is very difficult to trigger.
   On the flip side, it does allow for seamless restarts without relying on
   something external like msh to clean up the socket.

   If at some point sticky sockets become an option, this function should be
   removed. But that is not expected to happen soon, so for now, it's here. */

int uc_listen(int fd, const char* path, int backlog)
{
	int ret;
	struct sockaddr_un addr;

	if((ret = uc_address(&addr, path)) < 0)
		goto err;
	if((ret = sys_bind(fd, &addr, sizeof(addr))) >= 0)
		goto got;
	if(ret != -EADDRINUSE)
		goto err;

	if(sys_connect(fd, &addr, sizeof(addr)) != -ECONNREFUSED)
		goto err;
	if((ret = sys_unlink(addr.path)) < 0)
		goto err;
	if((ret = sys_bind(fd, &addr, sizeof(addr))) < 0)
		goto err;
got:
	ret = sys_listen(fd, backlog);
err:
	return ret;
}
