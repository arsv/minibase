#include <output.h>

struct heap;
struct ucbuf;

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

void top_init(CTX);
struct ucmsg* send_recv(CTX);
struct ucmsg* send_check(CTX);
void send_check_empty(CTX);

void dump_status(CTX, MSG);
void dump_scanlist(CTX, MSG);
void dump_linkconf(CTX, MSG);
struct ucattr** make_scanlist(CTX, MSG);

void put_psk_input(CTX, void* ssid, int slen);

void init_heap_socket(CTX);
void connect_to_wimon(CTX);

void init_output(CTX);
void fini_output(CTX);
void output(CTX, char* buf, int len);
