#include <bits/types.h>

/* Incoming packet analysis */

struct nlgen;
struct nlattr;

char* nl_str(struct nlattr* at);
uint16_t* nl_u16(struct nlattr* at);
uint32_t* nl_u32(struct nlattr* at);
uint64_t* nl_u64(struct nlattr* at);
struct nlattr* nl_nest(struct nlattr* at);

struct nlattr* nl_attr_0_in(char* buf, int len);
struct nlattr* nl_attr_n_in(char* buf, int len, struct nlattr* at);
struct nlattr* nl_attr_k_in(char* buf, int len, int type);

int nl_check_zstr(char* buf, int len);
int nl_check_nest(char* buf, int len);

int nl_attr_len(struct nlattr* at);
int nl_attr_is_nest(struct nlattr* at);
int nl_attr_is_zstr(struct nlattr* at);

/* GENL shorthands */

void* nl_get_of_len(struct nlgen* msg, uint16_t type, int len);
char* nl_get_str(struct nlgen* msg, uint16_t type);
struct nlattr* nl_get_nest(struct nlgen* msg, uint16_t type);

#define nl_get_int(at, kk, tt) (tt*)(nl_get_of_len(at, kk, sizeof(tt)))
#define nl_get_u16(at, kk) nl_get_int(at, kk, uint16_t)
#define nl_get_u32(at, kk) nl_get_int(at, kk, uint32_t)
#define nl_get_u64(at, kk) nl_get_int(at, kk, uint64_t)
#define nl_get_i32(at, kk) nl_get_int(at, kk, int32_t)

int nl_attr_len(struct nlattr* at);
void* nl_sub_of_len(struct nlattr* at, uint16_t type, int len);
char* nl_sub_str(struct nlattr* at, uint16_t type);

#define nl_sub_int(at, kk, tt) (tt*)(nl_sub_of_len(at, kk, sizeof(tt)))
#define nl_sub_u32(at, kk) nl_sub_int(at, kk, uint32_t)
#define nl_sub_i32(at, kk) nl_sub_int(at, kk, int32_t)

struct nlattr* nl_sub(struct nlattr* at, uint16_t type);
struct nlattr* nl_sub_0(struct nlattr* at);
struct nlattr* nl_sub_n(struct nlattr* at, struct nlattr* curr);
