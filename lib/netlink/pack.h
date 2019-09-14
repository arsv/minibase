#include <bits/types.h>

struct nlattr;

struct ncbuf {
	void* brk;
	void* ptr;
	void* end;
};

void nc_buf_set(struct ncbuf* nc, void* buf, uint size);

void nc_header(struct ncbuf* nc, int type, int flags, int seq);

void* nc_fixed(struct ncbuf* nc, uint hdrsize);
void nc_gencmd(struct ncbuf* nc, int cmd, int ver);

void nc_put(struct ncbuf* nc, uint key, void* data, uint size);
void nc_put_int(struct ncbuf* nc, uint key, int val);
void nc_put_str(struct ncbuf* nc, uint key, char* str);
void nc_put_flag(struct ncbuf* nc, uint key);
void nc_put_byte(struct ncbuf* nc, uint key, byte val);

int nc_send(int fd, struct ncbuf* nc);

int nc_recv_ack(int fd, void* buf, int len, int seq);

int nn_recv(int fd, void* buf, int len);

struct nlattr* nc_put_nest(struct ncbuf* nc, uint16_t type);
void nc_end_nest(struct ncbuf* nc, struct nlattr* at);

struct nlmsg* nc_message(struct ncbuf* nc);

int nl_subscribe(int fd, int id);
