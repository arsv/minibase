#include <bits/socket.h>
#include <bits/types.h>
#include <bits/iovec.h>
#include <syscall.h>

#define SHUT_RD   0
#define SHUT_WR   1
#define SHUT_RDWR 2

struct msghdr {
	void* name;
	unsigned namelen;
	struct iovec* iov;
	size_t iovlen;
	void* control;
	size_t controllen;
	int flags;
};

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

inline static long sys_recv(int fd, void* buf, size_t len, int flags)
{
	return syscall6(__NR_recvfrom, fd, (long)buf, len, flags, 0, 0);
}

inline static long sys_recvfrom(int fd, void* buf, size_t len, int flags,
		void* addr, int* alen)
{
	return syscall6(__NR_recvfrom, fd, (long)buf, len, flags,
			(long)addr, (long)alen);
}

inline static long sys_recvmsg(int fd, struct msghdr* msg, int flags)
{
	return syscall3(__NR_recvmsg, fd, (long)msg, flags);
}

inline static long sys_send(int fd, const char* buf, int len, int flags)
{
	return syscall6(__NR_sendto, fd, (long)buf, len, flags, 0, 0);
}

inline static long sys_sendto(int fd, const void* buf, int len, int flags,
		void* addr, int addrlen)
{
	return syscall6(__NR_sendto, fd, (long)buf, len, flags,
			(long)addr, addrlen);
}

inline static long sys_sendmsg(int fd, const struct msghdr* msg, int flags)
{
	return syscall3(__NR_sendmsg, fd, (long)msg, flags);
}
