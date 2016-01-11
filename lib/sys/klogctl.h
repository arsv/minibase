#include <bits/syscall.h>
#include <syscall.h>

inline static long sysklogctl(int op, char* buf, long len)
{
	return syscall3(__NR_syslog, op, (long) buf, len);
}
