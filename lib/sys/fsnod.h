#include <syscall.h>
#include <bits/fcntl.h>
#include <bits/time.h>

/* uapi/linux/fs.h */
#define RENAME_NOREPLACE  (1<<0)
#define RENAME_EXCHANGE   (1<<1)
#define RENAME_WHITEOUT   (1<<2)

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
