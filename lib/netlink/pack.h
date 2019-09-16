#include <bits/types.h>

/* NL command buffer, and routines for packing outbound messages.

   Intended usage for GENL:

	struct ncbuf nc;

	nc_buf_set(&nc, buf, sizeof(buf));

	nc_header(&nc, family, flags, seq);
	nc_gencmd(&nc, command, version);
	nc_put_...(&nc, ...);
	nc_put_...(&nc, ...);

	if((ret = nc_send(fd, &nc)) < 0)
		fail(...);

   RTNL messages need custom headers:

	struct rthdr* req;

	nc_header(&nc, command, flags, seq);

	if(!(req = nc_fixed(&nc, sizeof(*req))))
		goto send;

	req->foo = ...
	req->bar = ...

	...
send:
	if((ret = nc_send(fd, &nc)) < 0)
		fail(...);

   Overflow, if happens, is handled in nc_send(). */

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

struct nlattr* nc_put_nest(struct ncbuf* nc, uint16_t type);
void nc_end_nest(struct ncbuf* nc, struct nlattr* at);

int nc_send(int fd, struct ncbuf* nc);

/* For debugging purposes, it may be useful to get the outbound message
   as struct nlmsg (to pass it to nl_dump_genl for instance). Note just
   casting nc->buf is not enough because the length of the message does
   not get set until nc_send(). */

struct nlmsg* nc_msg(struct ncbuf* nc);
