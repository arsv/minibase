#include <bits/types.h>
#include <bits/iovec.h>
#include <bits/socket.h>
#include <syscall.h>

struct msghdr {
	void* name;
	unsigned namelen;
	struct iovec* iov;
	size_t iovlen;
	void* control;
	size_t controllen;
	int flags;
};

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
