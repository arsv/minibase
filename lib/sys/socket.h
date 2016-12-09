#include <syscall.h>
#include <bits/socket.h>

inline static long syssocket(int domain, int type, int proto)
{
	return syscall3(__NR_socket, domain, type, proto);
}
