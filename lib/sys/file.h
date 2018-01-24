#include <syscall.h>
#include <bits/stat.h>
#include <bits/types.h>
#include <bits/fcntl.h>
#include <bits/stdio.h>

/* Common file ops. */

inline static long sys_open(const char* name, int flags)
{
	return syscall3(NR_openat, AT_FDCWD, (long)name, flags);
}

inline static long sys_open3(const char* name, int flags, int mode)
{
	return syscall4(NR_openat, AT_FDCWD, (long)name, flags, mode);
}

inline static long sys_openat(int at, const char* path, int flags)
{
	return syscall3(NR_openat, at, (long)path, flags);
}

inline static long sys_openat4(int at, const char* path, int flags, int mode)
{
	return syscall4(NR_openat, at, (long)path, flags, mode);
}

inline static long sys_close(int fd)
{
	return syscall1(NR_close, fd);
}

inline static long sys_read(int fd, void* buf, unsigned long len)
{
	return syscall3(NR_read, fd, (long)buf, len);
}

inline static long sys_write(int fd, const void* buf, int len)
{
	return syscall3(NR_write, fd, (long)buf, len);
}

inline static long sys_fcntl(int fd, int cmd)
{
	return syscall2(NR_fcntl, fd, cmd);
}

inline static long sys_fcntl3(int fd, int cmd, int arg)
{
	return syscall3(NR_fcntl, fd, cmd, arg);
}

inline static long sys_dup(int fd)
{
	return syscall1(NR_dup, fd);
}

inline static long sys_dup2(int fda, int fdb)
{
	return syscall3(NR_dup3, fda, fdb, 0);
}

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define SEEK_DATA 3
#define SEEK_HOLE 4

/* There's probably no good way to implement lseek in a way that would
   work reasonably well on both 32 and 64 bit targets. The code below
   is the best I can come up with. The compiler should be able to eliminate
   the pointer mess in 64-bit case. */

#ifdef NR__llseek
inline static long sys_seek(int fd, int64_t off)
{
	int64_t pos;
	int32_t hi = (off >> 32);
	int32_t lo = (int32_t)off;

	return syscall5(NR__llseek, fd, hi, lo, (long)&pos, SEEK_SET);
}
#else
inline static long sys_seek(int fd, int64_t off)
{
	long ret;

	if((ret = syscall3(NR_lseek, fd, off, SEEK_SET)) < 0)
		return ret;

	return 0;
}
#endif

/* The prototype for this accidentally matches the man page for _llseek,
   so let's call it sys_llseek. */

#ifdef NR__llseek
inline static long sys_llseek(int fd, int64_t off, int64_t* pos, int whence)
{
	int32_t hi = (off >> 32);
	int32_t lo = (int32_t)off;

	return syscall5(NR__llseek, fd, hi, lo, (long)pos, whence);
}
#else
inline static long sys_llseek(int fd, int64_t off, int64_t* pos, int whence)
{
	long ret;

	if((ret = syscall3(NR_lseek, fd, off, whence)) < 0)
		return ret;

	*pos = ret;

	return 0;
}
#endif

#ifdef NR_newfstatat
# define NR_fstatat NR_newfstatat
#else
# define NR_fstatat NR_fstatat64
#endif

inline static long sys_stat(const char *path, struct stat *st)
{
	return syscall4(NR_fstatat, AT_FDCWD, (long)path, (long)st, 0);
}

inline static long sys_fstat(int fd, struct stat* st)
{
#ifdef NR_fstat64
	return syscall2(NR_fstat64, fd, (long)st);
#else
	return syscall2(NR_fstat, fd, (long)st);
#endif
}

inline static long sys_fstatat(int dirfd, const char *path,
                               struct stat* st, int flags)
{
	return syscall4(NR_fstatat, dirfd, (long)path, (long)st, flags);
}

inline static long sys_lstat(const char *path, struct stat *st)
{
	return syscall4(NR_fstatat, AT_FDCWD, (long)path, (long)st,
                                                     AT_SYMLINK_NOFOLLOW);
}

inline static long sys_pipe(int* fds)
{
	return syscall2(NR_pipe2, (long)fds, 0);
}

inline static long sys_pipe2(int* fds, int flags)
{
	return syscall2(NR_pipe2, (long)fds, flags);
}

#ifdef NR_pread64
#define NR_pread NR_pread64
#define NR_pwrite NR_pwrite64
#endif

inline static long sys_pread(int fd, void* buf, ulong len, uint64_t off)
{
#if BITS == 32
	union { uint64_t ll; long l[2]; } x = { .ll = off };
	return syscall5(NR_pread, fd, (long)buf, len, x.l[0], x.l[1]);
#else
	return syscall4(NR_pread, fd, (long)buf, (long)len, off);
#endif
}

inline static long sys_pwrite(int fd, void* buf, ulong len, uint64_t off)
{
#if BITS == 32
	union { uint64_t ll; long l[2]; } x = { .ll = off };
	return syscall5(NR_pwrite, fd, (long)buf, len, x.l[0], x.l[1]);
#else
	return syscall4(NR_pwrite, fd, (long)buf, (long)len, off);
#endif
}
