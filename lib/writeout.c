#include "writeall.h"
#include "writeout.h"
#include "memcpy.h"

/* Common code for buffered output to stdout.

   The assumption behind this is that a lot of utils
       * write text in small blocks, and
       * write it to stdout only.
   So instead of providing generic arbitrary fd buffered writes
   like stdio does, we implement this particular case only.
 
   It would be nice to keep its memory footprint reasonably
   low and preferably page-aligned. Why? Well there should be no need
   to make the buffer larger, and making it smaller would mean wasting
   space as it is not likely that there will be any other smaller
   global structures around.  

   In cases where larger buffer *is* needed, some other code should
   be used instead.
 
   Because of the weird alignment issues in gcc/ld, declaring
   the aux struct alongside the buffer is not easy. So instead
   we place the struct at the start of the buffer itself. */

static char outbuf[4096];

struct outbuf {
	long ptr;
	char buf[];
} *const out = (struct outbuf*) outbuf;

static const int outfd = 1; /* stdout */
static const int outlen = sizeof(outbuf) - sizeof(struct outbuf);

/* Both functions return errno which must be checked by the caller.
   That's probably redundant but I don't like the idea of calling
   fail() from anywhere in the library. */

long writeout(char* data, int len)
{
	int rem = outlen - out->ptr;
	long ret = 0;

	if(len <= rem) {
		/* the best case, all the data fits in buffer */
		memcpy(out->buf + out->ptr, data, len);
		if(len < rem) {
			out->ptr += len;
		} else {
			ret = writeall(outfd, out->buf, out->ptr + len);
			out->ptr = 0;
		}
		return ret;
	}

	if(out->ptr) {
		/* the data is too long but we must flush whatever's
		   in the buffer first */
		memcpy(out->buf + out->ptr, data, rem);
		ret = writeall(outfd, out->buf, out->ptr + rem);
		data += rem;
		len -= rem;
		out->ptr = 0;
	}

	/* at this point (out->ptr == 0) */

	if(len >= outlen) {
		int tail = len % outlen;
		int pass = len - tail;
		ret = writeall(outfd, data, pass);
		data += pass;
		len = tail;
	};
	
	/* at this point (len < out->len) && (out->ptr == 0) */

	if(len) {
		memcpy(out->buf, data, len);
		out->ptr = len;
	}

	return ret;
}

long flushout(void)
{
	long ret = 0;

	if(out->ptr) {
		ret = writeall(outfd, out->buf, out->ptr);
		out->ptr = 0;
	};

	return ret;
}
