#include <bits/socket.h>
#include <syscall.h>

inline static long syssocketpair(int af, int sock, int prot, int* fds)
{
	return syscall4(__NR_socketpair, af, sock, prot, (long)fds);
}
