//#include <output.h>
#include <nlusctl.h>

struct top {
	int opts;
	int argc;
	int argi;
	char** argv;

	int fd;
	int connected;

	struct ucbuf uc;
	struct urbuf ur;
	//struct bufout bo;

	char smallbuf[128];
};

#define CTX struct top* ctx
#define MSG struct ucmsg* msg
#define AT struct ucattr* at
#define UC (&ctx->uc)

typedef struct ucattr* attr;

//void output(CTX, char* buf, int len);
//void flush_output(CTX);

void init_socket(CTX);
void start_request(CTX, int cmd, int count, int length);
void add_str_attr(CTX, int key, char* name);
void add_int_attr(CTX, int key, int val);
void send_request(CTX);
struct ucmsg* recv_reply(CTX);

void dump_list(CTX, MSG);
void dump_info(CTX, MSG);
void dump_pid(CTX, MSG);
void dump_msg(CTX, MSG);
