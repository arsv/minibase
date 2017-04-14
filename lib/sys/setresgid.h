#include <syscall.h>

inline static long sys_setresgid(int rgid, int egid, int sgid)
{
	return syscall3(__NR_setresgid, rgid, egid, sgid);
}
