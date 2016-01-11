#include <syscall.h>
#include <bits/uname.h>

inline static long sysuname(struct utsname* buf)
{
	return syscall1(__NR_uname, (long)buf);
}
