#include <output.h>
#include <nlusctl.h>

struct ucbuf;
struct urbuf;

struct heap {
	void* brk;
	void* ptr;
	void* end;
};

struct top {
	int opts;
	int argc;
	int argi;
	char** argv;

	int fd;
	int connected;

	struct ucbuf uc;
	struct urbuf ur;
	struct bufout bo;

	char smallbuf[128];
	struct heap hp;
};

#define CTX struct top* ctx
#define MSG struct ucmsg* msg
#define AT struct ucattr* at
#define UC (&ctx->uc)

typedef struct ucattr* attr;

void output(CTX, char* buf, int len);
void flush_output(CTX);

void init_socket(CTX);
void expect_large(CTX);
void start_request(CTX, int cmd, int count, int length);
void send_request(CTX);
struct ucmsg* recv_reply(CTX);
void* heap_alloc(CTX, int size);

void dump_list(CTX, MSG);
void dump_info(CTX, MSG);
void dump_pid(CTX, MSG);
void dump_msg(CTX, MSG);
