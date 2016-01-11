#include <syscall.h>
#include <bits/klog.h>

inline static long sysklogctl(int op, char* buf, long len)
{
	return syscall3(__NR_syslog, op, (long) buf, len);
}
