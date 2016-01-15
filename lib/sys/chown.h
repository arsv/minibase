#include <syscall.h>

inline static long syschown(const char* filename, int uid, int gid)
{
	return syscall3(__NR_chown, (long)filename, uid, gid);
}
