#include <bits/socket.h>
#include <syscall.h>

#define SHUT_RD   0
#define SHUT_WR   1
#define SHUT_RDWR 2

inline static long sys_socket(int domain, int type, int proto)
{
	return syscall3(__NR_socket, domain, type, proto);
}

inline static long sys_socketpair(int af, int sock, int prot, int* fds)
{
	return syscall4(__NR_socketpair, af, sock, prot, (long)fds);
}

inline static long sys_bind(int fd, void* addr, int len)
{
	return syscall3(__NR_bind, fd, (long)addr, len);
}

inline static long sys_accept(int fd, void* addr, int* len)
{
	return syscall3(__NR_accept, fd, (long)addr, (long)len);
}

inline static long sys_listen(int fd, int backlog)
{
	return syscall2(__NR_listen, fd, backlog);
}

inline static long sys_connect(int fd, void* addr, int len)
{
	return syscall3(__NR_connect, fd, (long)addr, len);
}

inline static long sys_shutdown(int fd, int how)
{
	return syscall2(__NR_shutdown, fd, how);
}

inline static long sys_getsockopt(int fd, int lvl, int opt, void* val, int* len)
{
	return syscall5(__NR_getsockopt, fd, lvl, opt, (long)val, (long)len);	
}

inline static long sys_setsockopt(int fd, int lvl, int opt, void* val, int len)
{
	return syscall5(__NR_setsockopt, fd, lvl, opt, (long)val, len);
}

inline static long sys_setsockopti(int fd, int lvl, int opt, int val)
{
	return syscall5(__NR_setsockopt, fd, lvl, opt, (long)&val, sizeof(val));
}
