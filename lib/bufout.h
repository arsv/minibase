#ifndef __BUFOUT_H__
#define __BUFOUT_H__

/* Buffered output. Allocating the buffer itself is caller's duty
   as it may happen to be in the stack or somewhere else. */

struct bufout {
	int fd;
	int len;
	int ptr;
	char* buf;
};

long bufout(struct bufout* bo, char* data, int len);
long bufoutflush(struct bufout* bo);

#endif
