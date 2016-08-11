#include <syscall.h>

inline static long syschown(const char* filename, int uid, int gid)
{
#ifdef __NR_fchownat
	return syscall5(__NR_fchownat, AT_FDCWD, (long)filename, uid, gid, 0);
#else
	return syscall3(__NR_chown, (long)filename, uid, gid);
#endif
}
