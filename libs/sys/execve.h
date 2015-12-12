#include <bits/syscall.h>
#include <syscall3.h>

inline static long sysexecve(const char* exe, char** argv, char** envp)
{
	return syscall3(__NR_execve, (long)exe, (long)argv, (long)envp);
}
