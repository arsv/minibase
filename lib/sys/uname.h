#include <bits/syscall.h>
#include <syscall.h>

struct utsname;

inline static long sysuname(struct utsname* buf)
{
	return syscall1(__NR_uname, (long)buf);
}
