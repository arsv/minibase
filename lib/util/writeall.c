#include <sys/file.h>
#include <util.h>

/* We return EPIPE here to indicate incomplete write.
   In all concievable case that should be the only possible
   cause (and we'll probably get SIGPIPE anyway) */

long writeall(int fd, const void* buf, long len)
{
	long wr = 0;

	while(len > 0) {
		wr = sys_write(fd, buf, len);

		if(!wr)
			wr = -EPIPE;
		if(wr < 0)
			break;

		buf += wr;
		len -= wr;
	}

	return wr;
}
