#include <syscall.h>

inline static long syslisten(int fd, int backlog)
{
	return syscall2(__NR_listen, fd, backlog);
}
