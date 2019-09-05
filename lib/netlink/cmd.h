#include <bits/types.h>

struct ncbuf {
	void* brk;
	void* ptr;
	void* end;
};

void* nc_struct(struct ncbuf* nc, void* buf, uint size, uint hdrsize);
void nc_header(struct nlmsg* msg, int type, int flags, int seq);
void nc_length(struct nlmsg* msg, struct ncbuf* nc);

void nc_put(struct ncbuf* nc, uint key, void* data, uint size);
void nc_put_int(struct ncbuf* nc, uint key, int val);

int nc_send(int fd, struct ncbuf* nc);

int nc_recv_ack(int fd, void* buf, int len, int seq);
