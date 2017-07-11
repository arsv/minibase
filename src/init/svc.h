#include <output.h>
#include <nlusctl.h>
#include <heap.h>

struct heap;
struct ucbuf;
struct urbuf;

struct top {
	int opts;
	int argc;
	int argi;
	char** argv;

	int fd;
	struct heap hp;
	struct ucbuf uc;
	struct urbuf ur;
	int connected;
	char cbuf[128];

	struct bufout bo;
};

#define CTX struct top* ctx
#define MSG struct ucmsg* msg
#define AT struct ucattr* at
#define UC (&ctx->uc)

typedef struct ucattr* attr;

void init_output(CTX);
void fini_output(CTX);
void output(CTX, char* buf, int len);

void init_socket(CTX);
void init_recv_small(CTX);
void init_recv_heap(CTX);
void send_command(CTX);
struct ucmsg* recv_reply(CTX);

void dump_list(CTX, MSG);
void dump_info(CTX, MSG);
void dump_pid(CTX, MSG);
void dump_msg(CTX, MSG);
