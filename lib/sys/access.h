#include <syscall.h>
#include <bits/fcntl.h>
#include <bits/time.h>

#define R_OK  4
#define W_OK  2
#define X_OK  1
#define F_OK  0

#define UTIME_NOW   ((1l << 30) - 1l)
#define UTIME_OMIT  ((1l << 30) - 2l)

inline static long sys_access(const char* path, int mode)
{
	return syscall4(__NR_faccessat, AT_FDCWD, (long)path, mode, 0);
}

inline static long sys_chmod(const char* path, int mode)
{
#ifdef __NR_fchmodat
	return syscall4(__NR_fchmodat, AT_FDCWD, (long)path, mode, 0);
#else
	return syscall2(__NR_chmod, (long)path, mode);
#endif
}

inline static long sys_chown(const char* path, int uid, int gid)
{
#ifdef __NR_fchownat
	return syscall5(__NR_fchownat, AT_FDCWD, (long)path, uid, gid, 0);
#else
	return syscall3(__NR_chown, (long)path, uid, gid);
#endif
}

inline static long sys_utimensat(int at, char* path,
                                 const struct timespec times[2], int flags)
{
	return syscall4(__NR_utimensat, at, (long)path, (long)times, flags);
}

inline static long sys_utimes(int fd, const struct timespec times[2])
{
	return syscall4(__NR_utimensat, fd, 0, (long)times, 0);
}
