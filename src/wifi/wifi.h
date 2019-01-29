#include <output.h>

struct ucbuf;

struct top {
	int opts;
	int argc;
	int argi;
	char** argv;

	void* brk;
	void* ptr;

	byte ssid[32];
	int slen;
	byte psk[32];

	int fd;
	struct ucbuf uc;
	struct urbuf ur;
	int connected;
	char cbuf[128];

	struct bufout bo;

	int showbss;
	int unsaved;
};

#define CTX struct top* ctx
#define MSG struct ucmsg* msg
#define AT struct ucattr* at
#define UC (&ctx->uc)

typedef struct ucattr* attr;

void top_init(CTX);
void send_command(CTX);
int send_recv_cmd(CTX);
int send_recv_act(CTX);
struct ucmsg* send_recv_msg(CTX);
struct ucmsg* recv_reply(CTX);
void find_wifi_device(char out[32]);

void dump_status(CTX, MSG);
void dump_scanlist(CTX, MSG);
void dump_linkconf(CTX, MSG);
struct ucattr** make_scanlist(CTX, MSG);

void put_psk_input(CTX, void* ssid, int slen);

int connect_to_wictl(CTX);

void warn_sta(CTX, char* text, MSG);

void load_or_ask_psk(CTX);
void maybe_store_psk(CTX);
void remove_psk_entry(CTX);
void list_saved_psks(CTX);

void* heap_alloc(CTX, uint size);
