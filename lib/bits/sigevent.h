#ifndef __BITS_SIGEVENT_H__
#define __BITS_SIGEVENT_H__

#define SIGEV_SIGNAL    0
#define SIGEV_NONE      1
#define SIGEV_THREAD    2
#define SIGEV_THREAD_ID 4

union sigval {
	int integer;
	void* pointer;
};

struct sigevent {
	union sigval value;
	int signo;
	int notify;
	byte pad[64 - 2*4 - sizeof(void*)];
};

/* sizeof(struct sigevent) should be 64 apparently? */

#endif
