#include <syscall.h>
#include <bits/fcntl.h>
#include <bits/time.h>

/* File properties (mode and times). The opposite of *stat(). */

#define R_OK  4
#define W_OK  2
#define X_OK  1
#define F_OK  0

#define UTIME_NOW   ((1l << 30) - 1l)
#define UTIME_OMIT  ((1l << 30) - 2l)

inline static long sys_access(const char* path, int mode)
{
	return syscall4(NR_faccessat, AT_FDCWD, (long)path, mode, 0);
}

inline static long sys_chmod(const char* path, int mode)
{
#ifdef NR_fchmodat
	return syscall4(NR_fchmodat, AT_FDCWD, (long)path, mode, 0);
#else
	return syscall2(NR_chmod, (long)path, mode);
#endif
}

inline static long sys_chown(const char* path, int uid, int gid)
{
#ifdef NR_fchownat
	return syscall5(NR_fchownat, AT_FDCWD, (long)path, uid, gid, 0);
#else
	return syscall3(NR_chown, (long)path, uid, gid);
#endif
}

inline static long sys_fchown(int fd, int uid, int gid)
{
#ifdef NR_fchownat
	char* empty = "";
	int flags = AT_EMPTY_PATH;
	return syscall5(NR_fchownat, fd, (long)empty, uid, gid, flags);
#else
	return syscall3(NR_fchown, fd, uid, gid);
#endif
}

inline static long sys_utimensat(int at, char* path,
                                 const struct timespec times[2], int flags)
{
	return syscall4(NR_utimensat, at, (long)path, (long)times, flags);
}

inline static long sys_utimes(int fd, const struct timespec times[2])
{
	return syscall4(NR_utimensat, fd, 0, (long)times, 0);
}

#define FALLOC_FL_KEEP_SIZE        1
#define FALLOC_FL_PUNCH_HOLE       2
#define FALLOC_FL_COLLAPSE_RANGE   8
#define FALLOC_FL_ZERO_RANGE      16

inline static long sys_fallocate(int fd, int mode, uint64_t offset, uint64_t len)
{
	return syscall4(NR_fallocate, fd, mode, offset, len);
}

inline static long sys_truncate(const char* path, uint64_t size)
{
	return syscall2(NR_truncate, (long)path, size);
}

inline static long sys_ftruncate(int fd, uint64_t size)
{
	return syscall2(NR_ftruncate, fd, size);
}
