#include <cdefs.h>

/* (Netlink-based) userspace control protocol for services */

struct ucbuf {
	void* buf;
	int len;
};

struct ucaux {
	int ptr;
	byte buf[64];
};

struct ucattr {
	uint16_t len;
	uint16_t key;
	byte payload[];
} __packed;

/* external structs */

struct iovec;

/* socket creation */

int uc_listen(int fd, const char* path, int backlog);
int uc_connect(int fd, const char* path);

/* sending and recieving messages */

int uc_recv(int fd, void* buf, int len);
int uc_recv_aux(int fd, void* buf, int len, struct ucaux* ux);

int uc_send(int fd, struct ucbuf* uc);
int uc_send_aux(int fd, struct ucbuf* uc, struct ucaux* ux);
int uc_send_iov(int fd, struct iovec* iov, int iovcnt);

int uc_iov_hdr(struct iovec* iov, struct ucbuf* uc);
struct ucattr* uc_msg(void* buf, int len);

int uc_wait_writable(int fd);

/* packing message to be sent */

void uc_buf_set(struct ucbuf* uc, void* buf, int len);
void uc_put_hdr(struct ucbuf* uc, int cmd);

struct ucattr* uc_put(struct ucbuf* uc, int key, int len, int extra);
void uc_put_bin(struct ucbuf* uc, int key, void* buf, int len);
void uc_put_int(struct ucbuf* uc, int key, int v);
void uc_put_str(struct ucbuf* uc, int key, char* str);
void uc_put_flag(struct ucbuf* uc, int key);
void uc_put_i64(struct ucbuf* uc, int key, int64_t v);
void uc_put_tail(struct ucbuf* uc, int key, int len);
void uc_put_strn(struct ucbuf* uc, int key, char* src, int max);

struct ucattr* uc_put_nest(struct ucbuf* uc, int key);
void uc_end_nest(struct ucbuf* uc, struct ucattr* nest);

struct ucattr* uc_put_strs(struct ucbuf* uc, int key);
void uc_add_str(struct ucbuf* uc, const char* str);
void uc_end_strs(struct ucbuf* uc, struct ucattr* at);

int uc_space_left(struct ucbuf* uc);

/* parsing recieved message */

int uc_repcode(struct ucattr* msg);
void* uc_payload(struct ucattr* at);
int uc_paylen(struct ucattr* at);

struct ucattr* uc_get_0(struct ucattr* msg);
struct ucattr* uc_get_n(struct ucattr* msg, struct ucattr* at);
int uc_is_keyed(struct ucattr* at, int key);

struct ucattr* uc_get(struct ucattr* msg, int key);
void* uc_get_bin(struct ucattr* msg, int key, int len);
int*  uc_get_int(struct ucattr* msg, int key);
char* uc_get_str(struct ucattr* msg, int key);

int64_t*  uc_get_i64(struct ucattr* msg, int key);
uint64_t* uc_get_u64(struct ucattr* msg, int key);

void* uc_to_bin(struct ucattr* at, int key, int len);
int*  uc_to_int(struct ucattr* at, int key);
char* uc_to_str(struct ucattr* at, int key);

/* debug output */

void uc_dump(struct ucattr* msg);

/* ancillary data */

void ux_putf1(struct ucaux* ux, int fd);
int ux_getf1(struct ucaux* ux);
