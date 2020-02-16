#ifndef __BITS_FCNTL_H__
#define __BITS_FCNTL_H__

#include <bits/oflags.h>

#define S_IFMT          0170000
#define S_IFDIR         0040000
#define S_IFCHR         0020000
#define S_IFBLK         0060000
#define S_IFREG         0100000
#define S_IFIFO         0010000
#define S_IFLNK         0120000
#define S_IFSOCK        0140000
#define S_ISUID         0004000
#define S_ISGID         0002000
#define S_ISVTX         0001000

#define S_IRWXU         0000700
#define S_IRUSR         0000400
#define S_IWUSR         0000200
#define S_IXUSR         0000100

#define S_IRWXG         0000070
#define S_IRGRP         0000040
#define S_IWGRP         0000020
#define S_IXGRP         0000010

#define S_IRWXO         0000007
#define S_IROTH         0000004
#define S_IWOTH         0000002
#define S_IXOTH         0000001

#define AT_FDCWD               -100
#define AT_REMOVEDIR          0x200
#define AT_EACCESS            0x200
#define AT_SYMLINK_NOFOLLOW   0x100
#define AT_NO_AUTOMOUNT       0x800
#define AT_EMPTY_PATH        0x1000

#define LOCK_SH          (1<<0)
#define LOCK_EX          (1<<1)
#define LOCK_NB          (1<<2)
#define LOCK_UN          (1<<3)
#define LOCK_MAND        (1<<5)
#define LOCK_READ        (1<<6)
#define LOCK_WRITE       (1<<7)

#define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m) (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m) (((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)

#define F_GETFD 1
#define F_SETFL 4

#define F_LINUX_SPECIFIC_BASE 1024
#define F_DUPFD_CLOEXEC	(F_LINUX_SPECIFIC_BASE + 6)

#endif
