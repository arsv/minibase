#include <syscall.h>
#include <bits/fcntl.h>
#include <bits/rename.h>

inline static long sysrenameat2(int olddirfd, const char* oldpath,
                                int newdirfd, const char* newpath,
                                unsigned int flags)
{
	return syscall5(__NR_renameat2, olddirfd, (long)oldpath,
                                        newdirfd, (long)newpath, flags);
}
