#include <bits/types.h>
#include <output.h>

struct top {
	int fd;

	byte id[2];
	byte rand[16];
	int avail;

	byte* data;
	uint size;
	uint len;
	uint ptr;

	struct bufout bo;

	uint nscount;
	byte nsaddr[4][8];
};

#define CTX struct top* ctx __unused

struct dnshdr;

struct dnshdr* run_request(CTX, char* name, ushort type);

void dump_answers(CTX, struct dnshdr* dh);
void fill_nsaddrs(CTX, struct dnshdr* dh);
