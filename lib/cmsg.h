#ifndef __CMSG_H__
#define __CMSG_H__

#include <cdefs.h>

#define SCM_RIGHTS      1
#define SCM_CREDENTIALS 2

struct cmsg {
	ulong len;
	int level;
	int type;
	char data[];
};

struct ucred {
	int pid;
	int uid;
	int gid;
};

ulong cmsg_align(ulong len);
struct cmsg* cmsg_first(void* p, void* e);
struct cmsg* cmsg_next(void* p, void* e);
void* cmsg_put(void* p, void* e, int lvl, int type, void* data, ulong len);

int cmsg_paylen(struct cmsg* cm);
void* cmsg_payload(struct cmsg* cm);
void* cmsg_paylend(struct cmsg* cm);

struct cmsg* cmsg_get(void* p, void* e, int lvl, int type);

#endif
