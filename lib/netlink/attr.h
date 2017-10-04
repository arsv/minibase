#include <bits/types.h>

/* Incoming packet analysis */

struct nlgen;
struct nlattr;

void* nl_bin(struct nlattr* at, unsigned len);
char* nl_str(struct nlattr* at);
struct nlattr* nl_nest(struct nlattr* at);

#define nl_int(at, tt) (tt*)(nl_bin(at, sizeof(tt)))
#define nl_u8(at) nl_int(at, uint8_t)
#define nl_u16(at) nl_int(at, uint16_t)
#define nl_u32(at) nl_int(at, uint32_t)
#define nl_u64(at) nl_int(at, uint64_t)

#define NLPAYLOAD(msg) msg->payload, (msg->nlm.len - sizeof(*msg))
#define ATPAYLOAD(at)  at->payload, (at->len - sizeof(*at))

struct nlattr* nl_attr_0_in(char* buf, size_t len);
struct nlattr* nl_attr_n_in(char* buf, size_t len, struct nlattr* at);
struct nlattr* nl_attr_k_in(char* buf, size_t len, int type);

int nl_check_zstr(char* buf, size_t len);
int nl_check_nest(char* buf, size_t len);

int nl_attr_len(struct nlattr* at);
int nl_attr_is_nest(struct nlattr* at);
int nl_attr_is_zstr(struct nlattr* at);

/* GENL shorthands */

struct nlattr* nl_get(struct nlgen* msg, uint16_t type);
void* nl_get_of_len(struct nlgen* msg, uint16_t type, size_t len);
char* nl_get_str(struct nlgen* msg, uint16_t type);
struct nlattr* nl_get_nest(struct nlgen* msg, uint16_t type);

#define nl_get_int(at, kk, tt) (tt*)(nl_get_of_len(at, kk, sizeof(tt)))
#define nl_get_u16(at, kk) nl_get_int(at, kk, uint16_t)
#define nl_get_u32(at, kk) nl_get_int(at, kk, uint32_t)
#define nl_get_u64(at, kk) nl_get_int(at, kk, uint64_t)
#define nl_get_i32(at, kk) nl_get_int(at, kk, int32_t)

int nl_attr_len(struct nlattr* at);
void* nl_sub_of_len(struct nlattr* at, uint16_t type, size_t len);
char* nl_sub_str(struct nlattr* at, uint16_t type);

#define nl_sub_int(at, kk, tt) (tt*)(nl_sub_of_len(at, kk, sizeof(tt)))
#define nl_sub_u32(at, kk) nl_sub_int(at, kk, uint32_t)
#define nl_sub_i32(at, kk) nl_sub_int(at, kk, int32_t)

struct nlattr* nl_sub(struct nlattr* at, uint16_t type);
struct nlattr* nl_sub_0(struct nlattr* at);
struct nlattr* nl_sub_n(struct nlattr* at, struct nlattr* curr);
