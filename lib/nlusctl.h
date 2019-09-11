#include <cdefs.h>

/* Netlink-based userspace service control protocol.

   Essentially simplified request-reply GENL with hairy stuff like
   notifications and multipart replies removed.

   This is kept separated from proper netlink code for now, in part
   because this is only expected to be used in half-duplex mode
   with a single rx/tx buffer. */

struct ucbuf {
	char* buf;
	char* ptr;
	char* end;
};

struct urbuf {
	char* buf;
	char* mptr;
	char* rptr;
	char* end;
	struct ucmsg* msg;
};

struct ucaux {
	void* buf;
	int len;
};

struct ucmsg {
	int32_t len;
	int32_t cmd;
	char payload[];
} __packed;

struct ucattr {
	uint16_t len;
	uint16_t key;
	char payload[];
} __packed;

void uc_buf_set(struct ucbuf* uc, void* buf, size_t len);

void ur_buf_set(struct urbuf* ur, void* buf, size_t len);
void ur_buf_change(struct urbuf* ur, void* newbuf, size_t newlen);

void uc_put_hdr(struct ucbuf* uc, int cmd);
void uc_put_end(struct ucbuf* uc);
int uc_overflow(struct ucbuf* uc);

void* uc_put_attr(struct ucbuf* uc, int key, size_t len);
void uc_put_bin(struct ucbuf* uc, int key, void* buf, size_t len);
void uc_put_int(struct ucbuf* uc, int key, int v);
void uc_put_str(struct ucbuf* uc, int key, char* str);
void uc_put_flag(struct ucbuf* uc, int key);
void uc_put_i64(struct ucbuf* uc, int key, int64_t v);

struct ucattr* uc_put_nest(struct ucbuf* uc, int key);
void uc_end_nest(struct ucbuf* uc, struct ucattr* nest);

int uc_msglen(char* buf, size_t len);
struct ucmsg* uc_msg(char* buf, size_t len);

struct ucattr* uc_get_0(struct ucmsg* msg);
struct ucattr* uc_get_n(struct ucmsg* msg, struct ucattr* at);

struct ucattr* uc_sub_0(struct ucattr* ab);
struct ucattr* uc_sub_n(struct ucattr* ab, struct ucattr* at);

struct ucattr* uc_get(struct ucmsg* msg, int key);
void* uc_get_bin(struct ucmsg* msg, int key, int len);
int*  uc_get_int(struct ucmsg* msg, int key);
char* uc_get_str(struct ucmsg* msg, int key);

int64_t*  uc_get_i64(struct ucmsg* msg, int key);
uint64_t* uc_get_u64(struct ucmsg* msg, int key);

struct ucattr* uc_sub(struct ucattr* at, int key);
void* uc_sub_bin(struct ucattr* at, int key, int len);
int*  uc_sub_int(struct ucattr* at, int key);
char* uc_sub_str(struct ucattr* at, int key);

void* uc_is_bin(struct ucattr* at, int key, int len);
int* uc_is_int(struct ucattr* at, int key);
char* uc_is_str(struct ucattr* at, int key);
struct ucattr* uc_is_nest(struct ucattr* at, int key);

void* uc_payload(struct ucattr* at);
int uc_paylen(struct ucattr* at);

void uc_dump(struct ucmsg* msg);
void uc_dump_rx(struct urbuf* ur);
void uc_dump_tx(struct ucbuf* uc);

int uc_recv_shift(int fd, struct urbuf* ur);
int uc_send_block(int fd, struct ucbuf* uc);
int uc_send_whole(int fd, struct ucbuf* uc);
int uc_send_msg(int fd, struct ucbuf* uc, void* ancillary, int alen);

int uc_recv_whole(int fd, void* buf, int len);
int uc_send_timed(int fd, struct ucbuf* uc);
int uc_recvmsg(int fd, void* buf, int len, struct ucaux* ux);
