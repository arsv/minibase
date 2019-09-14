#include <sys/socket.h>

#include <netlink/recv.h>

int nl_recv(int fd, void* buf, int len)
{
	return sys_recv(fd, buf, len, 0);
}
