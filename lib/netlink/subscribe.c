#include <sys/socket.h>
#include <netlink.h>
#include <netlink/pack.h>

int nl_subscribe(int fd, int id)
{
	int lvl = SOL_NETLINK;
	int opt = NETLINK_ADD_MEMBERSHIP;

	return sys_setsockopt(fd, lvl, opt, &id, sizeof(id));
}
