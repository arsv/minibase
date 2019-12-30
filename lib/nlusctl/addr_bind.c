#include <bits/socket/unix.h>
#include <sys/socket.h>
#include <sys/fpath.h>
#include <sys/creds.h>

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

/* Regarding umask use: in Linux, file permissions on local sockets do matter,
   but handling them is quite difficult, and considering the planned security
   model for this project, most likely pointless. It is pretty much decided
   that permissions will be set (statically) on the directory instead. To make
   that work, we force 0777 on sockets here, otherwise the missing bits would
   change effective permissions.

   There are no other ways of passing initial socket permissions in Linux
   afaik. In pretty much all cases, current umask will NOT be 0000, in fact
   using 0000 outside of this code is a pretty bad idea for most services.

   Note in BSD things work exactly like that, without any umask calls, because
   they just ignore file permissions on sockets. */

int uc_listen(int fd, const char* path, int backlog)
{
	int ret, mask = 0000;
	struct sockaddr_un addr;

	if((ret = sys_umask(mask)) < 0)
		goto err;

	mask = ret;

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
	if((ret = sys_umask(mask)) < 0)
		goto err;

	ret = sys_listen(fd, backlog);
err:
	return ret;
}
