#include <bits/types.h>

struct netlink;

/* Packet assembly; see wire.h for send-recv stuff. */

void* nl_alloc(struct netlink* nl, int size);
void* nl_start_packet(struct netlink* nl, int hdrsize);

/* This awful piece makes generic header setup somewhat palatable.
   Should only be used directly with irregular RTNL headers;
   common GENL headers allow for much simplier nl_genhdr(...).

   Usage:

       struct ifaddrmsg {
               struct nlmsg nlm;
	       ...field
	       ...field
	       ...field
	       char payload[]; // maybe
       };

       struct ifaddrmsg* req;

       nl_header(nl, req, RTM_NEWLINK, NLM_F_REPLACE,
                       .field = ...,
		       .field = ...
		       .field = ...);

   Having a pre-defined variable simplifies the substituted code
   and allows dumping the request during or right after assemly. */

#define nl_header(nl, vv, tt, ff, ...) \
	if((vv = (typeof(vv))nl_start_packet(nl, sizeof(*vv)))) \
		*vv = (typeof(*vv)) { \
			.nlm = { \
				.len = sizeof(*vv),\
				.type = tt,\
				.flags = ff | NLM_F_REQUEST,\
				.seq = (nl)->seq,\
				.pid = 0\
			}, __VA_ARGS__\
		}

void nl_new_cmd(struct netlink* nl, uint16_t fam, uint8_t cmd, uint8_t ver);

/* Attributes, must be called after nl_header or nl_genhdr */

void nl_put(struct netlink* nl, uint16_t type, const void* buf, int len);
void nl_put_str(struct netlink* nl, uint16_t type, const char* str);
void nl_put_u8(struct netlink* nl, uint16_t type, uint8_t val);
void nl_put_u32(struct netlink* nl, uint16_t type, uint32_t val);
void nl_put_u64(struct netlink* nl, uint16_t type, uint64_t val);
void nl_put_empty(struct netlink* nl, uint16_t type);

struct nlattr* nl_put_nest(struct netlink* nl, uint16_t type);
void nl_end_nest(struct netlink* nl, struct nlattr* at);
