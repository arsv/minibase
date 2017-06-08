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
	int connected;
	char cbuf[128];
};

#define CTX struct top* ctx
#define MSG struct ucmsg* msg
#define UC (&ctx->uc)

typedef struct ucattr* attr;

void top_init(struct top* ctx);
struct ucmsg* send_recv(struct top* ctx);
struct ucmsg* send_check(struct top* ctx);
void send_check_empty(struct top* ctx);

void dump_status(struct top* ctx, struct ucmsg* msg);
void dump_scanlist(struct top* ctx, struct ucmsg* msg);

void put_psk_arg(struct top* ctx, char* ssid, char* pass);
void put_psk_input(struct top* ctx, char* ssid);

void init_heap_socket(struct top* ctx);
void connect_to_wimon(struct top* ctx);
