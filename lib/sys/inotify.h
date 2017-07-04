#include <syscall.h>
#include <bits/ints.h>
#include <bits/fcntl.h>

#define IN_CLOEXEC       O_CLOEXEC
#define IN_NONBLOCK      O_NONBLOCK

#define IN_ACCESS        0x00000001
#define IN_MODIFY        0x00000002
#define IN_ATTRIB        0x00000004
#define IN_CLOSE_WRITE   0x00000008
#define IN_CLOSE_NOWRITE 0x00000010
#define IN_CLOSE         (IN_CLOSE_WRITE | IN_CLOSE_NOWRITE)
#define IN_OPEN          0x00000020
#define IN_MOVED_FROM    0x00000040
#define IN_MOVED_TO      0x00000080
#define IN_MOVE          (IN_MOVED_FROM | IN_MOVED_TO)
#define IN_CREATE        0x00000100
#define IN_DELETE        0x00000200
#define IN_DELETE_SELF   0x00000400
#define IN_MOVE_SELF     0x00000800
#define IN_ALL_EVENTS    0x00000fff

#define IN_UNMOUNT       0x00002000
#define IN_Q_OVERFLOW    0x00004000
#define IN_IGNORED       0x00008000

#define IN_ONLYDIR       0x01000000
#define IN_DONT_FOLLOW   0x02000000
#define IN_EXCL_UNLINK   0x04000000
#define IN_MASK_ADD      0x20000000

#define IN_ISDIR         0x40000000
#define IN_ONESHOT       0x80000000

struct inotify_event {
	int wd;
	uint32_t mask;
	uint32_t cookie;
	uint32_t len;
	char name[];
};

inline static long sys_inotify_init(void)
{
	return syscall1(__NR_inotify_init1, 0);
}

inline static long sys_inotify_init1(int flags)
{
	return syscall1(__NR_inotify_init1, flags);
}

inline static long sys_inotify_add_watch(int fd, const char* path, unsigned mask)
{
	return syscall3(__NR_inotify_add_watch, fd, (long)path, mask);
}

inline static long sys_inotify_rm_watch(int fd, int wd)
{
	return syscall2(__NR_inotify_rm_watch, fd, wd);
}
