#include <bits/errno.h>
#include <bits/fcntl.h>
#include <bits/time.h>
#include <syscall.h>

/* File path ops, stuff that mostly deals with directories. */

inline static long sys_chdir(const char* path)
{
	return syscall1(__NR_chdir, (long)path);
}

inline static long sys_fchdir(int fd)
{
	return syscall1(__NR_fchdir, fd);
}

inline static long sys_getcwd(char* buf, size_t size)
{
	return syscall2(__NR_getcwd, (long)buf, size);
}

inline static long sys_chroot(const char* dir)
{
	return syscall1(__NR_chroot, (long)dir);
}

inline static long sys_mkdir(const char* path, int mode)
{
	return syscall3(__NR_mkdirat, AT_FDCWD, (long)path, mode);
}

inline static long sys_mkdirat(int at, const char* path, int mode)
{
	return syscall3(__NR_mkdirat, at, (long)path, mode);
}

inline static long sys_mknod(const char* path, int mode, int dev)
{
	return syscall4(__NR_mknodat, AT_FDCWD, (long)path, mode, dev);
}

inline static long sys_mknodat(int at, const char* path, int mode, int dev)
{
	return syscall4(__NR_mknodat, at, (long)path, mode, dev);
}

inline static long sys_rmdir(const char* dir)
{
	return syscall3(__NR_unlinkat, AT_FDCWD, (long)dir, AT_REMOVEDIR);
}

inline static long sys_rmdirat(int at, const char* dir)
{
	return syscall3(__NR_unlinkat, at, (long)dir, AT_REMOVEDIR);
}

inline static long sys_unlink(const char* name)
{
	return syscall3(__NR_unlinkat, AT_FDCWD, (long)name, 0);
}

inline static long sys_unlinkat(int at, const char* name, int flags)
{
	return syscall3(__NR_unlinkat, at, (long)name, flags);
}

inline static long sys_symlink(const char *target, const char *path)
{
	return syscall3(__NR_symlinkat, (long)target, AT_FDCWD, (long)path);
}

inline static long sys_symlinkat(const char *target, int at, const char *path)
{
	return syscall3(__NR_symlinkat, (long)target, at, (long)path);
}

inline static long sys_readlink(const char* path, char* buf, long len)
{
#ifdef __NR_readlinkat
	return syscall4(__NR_readlinkat, AT_FDCWD, (long)path, (long)buf, len);
#else
	return syscall3(__NR_readlink, (long)path, (long)buf, len);
#endif
}

inline static long sys_readlinkat(int at, const char* path, char* buf, long len)
{
#ifdef __NR_readlinkat
	return syscall4(__NR_readlinkat, AT_FDCWD, (long)path, (long)buf, len);
#else
	return -ENOSYS;
#endif
}

/* uapi/linux/fs.h */
#define RENAME_NOREPLACE  (1<<0)
#define RENAME_EXCHANGE   (1<<1)
#define RENAME_WHITEOUT   (1<<2)

inline static long sys_rename(const char* oldpath, const char* newpath)
{
	return syscall5(__NR_renameat2, AT_FDCWD, (long)oldpath,
                                        AT_FDCWD, (long)newpath, 0);
}

inline static long sys_renameat2(int oldat, const char* oldpath,
                                 int newat, const char* newpath, int flags)
{
	return syscall5(__NR_renameat2, oldat, (long)oldpath,
                                        newat, (long)newpath, flags);
}
