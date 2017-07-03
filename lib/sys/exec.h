#include <syscall.h>

inline static long sys_execve(const char* exe, char** argv, char** envp)
{
	return syscall3(__NR_execve, (long)exe, (long)argv, (long)envp);
}
