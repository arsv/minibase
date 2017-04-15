#include <syscall.h>

inline static long sys_setgroups(int size, int* list)
{
	return syscall2(__NR_setgroups, size, (long)list);
}
