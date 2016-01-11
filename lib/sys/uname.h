#include <bits/syscall.h>
#include <syscall1.h>

struct utsname;

inline static long sysuname(struct utsname* buf)
{
	return syscall1(__NR_uname, (long)buf);
}
