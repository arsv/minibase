#ifndef __OUTPUT_H__
#define __OUTPUT_H__

#include <cdefs.h>

/* Buffered output. Allocating the buffer itself is caller's duty
   as it may happen to be in the stack or somewhere else. */

struct bufout {
	int fd;
	int len;
	int ptr;
	char* buf;
};

void bufoutset(struct bufout* bo, int fd, void* buf, uint len);

int bufout(struct bufout* bo, char* data, int len);
int bufoutflush(struct bufout* bo);

/* Buffered output with pre-defined extern buffer */

int writeout(char* data, int len);
int flushout(void);

#endif
