#ifndef __OUTPUT_H__
#define __OUTPUT_H__

#include <null.h>
#include <bits/stdio.h>

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

/* Buffered output with pre-defined extern buffer */

long writeout(char* data, int len);
long flushout(void);

#endif
