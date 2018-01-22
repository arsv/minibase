#include <syscall.h>
#include <bits/types.h>
#include <bits/fcntl.h>

#define IN_CLOEXEC       (1<<19)
#define IN_NONBLOCK      (1<<11)

#define IN_ACCESS        (1<<0)
#define IN_MODIFY        (1<<1)
#define IN_ATTRIB        (1<<2)
#define IN_CLOSE_WRITE   (1<<3)
#define IN_CLOSE_NOWRITE (1<<4)
#define IN_CLOSE         (IN_CLOSE_WRITE | IN_CLOSE_NOWRITE)
#define IN_OPEN          (1<<5)
#define IN_MOVED_FROM    (1<<6)
#define IN_MOVED_TO      (1<<7)
#define IN_MOVE          (IN_MOVED_FROM | IN_MOVED_TO)
#define IN_CREATE        (1<<8)
#define IN_DELETE        (1<<9)
#define IN_DELETE_SELF   (1<<10)
#define IN_MOVE_SELF     (1<<11)

#define IN_ALL_EVENTS    ((1<<12)-1)

#define IN_UNMOUNT       (1<<13)
#define IN_Q_OVERFLOW    (1<<14)
#define IN_IGNORED       (1<<15)
#define IN_ONLYDIR       (1<<24)
#define IN_DONT_FOLLOW   (1<<25)
#define IN_EXCL_UNLINK   (1<<26)
#define IN_MASK_ADD      (1<<29)
#define IN_ISDIR         (1<<30)
#define IN_ONESHOT       (1<<31)

struct inotify_event {
	int wd;
	uint32_t mask;
	uint32_t cookie;
	uint32_t len;
	char name[];
};

inline static long sys_inotify_init(void)
{
	return syscall1(NR_inotify_init1, 0);
}

inline static long sys_inotify_init1(int flags)
{
	return syscall1(NR_inotify_init1, flags);
}

inline static long sys_inotify_add_watch(int fd, const char* path, unsigned mask)
{
	return syscall3(NR_inotify_add_watch, fd, (long)path, mask);
}

inline static long sys_inotify_rm_watch(int fd, int wd)
{
	return syscall2(NR_inotify_rm_watch, fd, wd);
}
