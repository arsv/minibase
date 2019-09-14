#include <string.h>
#include <output.h>
#include <util.h>

/* Buffered output. Works like syswrite() but only actually calls syswrite
   with block of around bo->len size. */

void bufoutset(struct bufout* bo, int fd, void* buf, uint len)
{
	bo->fd = fd;
	bo->buf = buf;
	bo->ptr = 0;
	bo->len = len;
}

int bufout(struct bufout* bo, char* data, int len)
{
	int rem = bo->len - bo->ptr;
	int ret = 0;

	if(len <= rem) {
		/* the best case, all the data fits in buffer */
		memcpy(bo->buf + bo->ptr, data, len);
		if(len < rem) {
			bo->ptr += len;
		} else {
			ret = writeall(bo->fd, bo->buf, bo->ptr + len);
			bo->ptr = 0;
		}
		return ret;
	}

	if(bo->ptr) {
		/* the data is too long but we must flush whatever's
		   in the buffer first */
		memcpy(bo->buf + bo->ptr, data, rem);
		ret = writeall(bo->fd, bo->buf, bo->ptr + rem);
		data += rem;
		len -= rem;
		bo->ptr = 0;
	}

	/* at this point (bo->ptr == 0) */

	if(len >= bo->len) {
		int tail = len % bo->len;
		int pass = len - tail;
		ret = writeall(bo->fd, data, pass);
		data += pass;
		len = tail;
	};

	/* at this point (len < bo->len) && (bo->ptr == 0) */

	if(len) {
		memcpy(bo->buf, data, len);
		bo->ptr = len;
	}

	return ret;
}

int bufoutflush(struct bufout* bo)
{
	int ret = 0;

	if(bo->ptr) {
		ret = writeall(bo->fd, bo->buf, bo->ptr);
		bo->ptr = 0;
	};

	return ret;
}
