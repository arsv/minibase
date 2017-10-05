#include <bits/types.h>
#include <syscall.h>

#define DT_UNKNOWN  0
#define DT_FIFO     1
#define DT_CHR      2
#define DT_DIR      4
#define DT_BLK      6
#define DT_REG      8
#define DT_LNK     10
#define DT_SOCK    12
#define DT_WHT     14

/* Note this is 64-bit struct, sometimes (but not always) named dirent64,
   and the compatible 64-bit syscall. We do not care about the deprecated
   non-64-bit version. */

struct dirent {
	uint64_t ino;
	int64_t off;
	uint16_t reclen;
	uint8_t type;
	char name[];
};

/* The syscall is almost impossible to use without this little helper func. */

inline static int dotddot(char* p)
{
	if(!*p) return 0;
	if(*p++ != '.') return 0;
	if(!*p) return 1; /* . */
	if(*p++ != '.') return 0;
	if(!*p) return 1; /* .. */
	return 0;
}

inline static long sys_getdents(int fd, void* dp, int count)
{
	return syscall3(NR_getdents64, fd, (long)dp, count);
}
