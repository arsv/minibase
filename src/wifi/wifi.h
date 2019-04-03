#include <output.h>

#define AP_BADSSID 0
#define AP_NOCRYPT 1
#define AP_CANCONN 2

struct ucbuf;

struct config {
	int fd;
	uint len;
	void* buf;
};

struct top {
	int opts;
	int argc;
	int argi;
	char** argv;

	void* ptr;
	void* brk;

	uint slen;
	byte ssid[32];
	byte psk[32];

	int fd;
	struct ucbuf uc;
	struct urbuf ur;
	int connected;
	char rxbuf[128];
	char txbuf[128];

	struct bufout bo;

	int showbss;
	int unsaved;

	struct config cfg;
};

#define CTX struct top* ctx __attribute__((unused))
#define MSG struct ucmsg* msg
#define AT struct ucattr* at
#define UC (&ctx->uc)

typedef struct ucattr* attr;

void find_wifi_device(char out[32]);

void dump_status(CTX, MSG);
void dump_scanlist(CTX, MSG);

void warn_sta(CTX, char* text, MSG);
void warn_bss(CTX, char* text, MSG);

int load_saved_psk(CTX);
void ask_passphrase(CTX);
void maybe_store_psk(CTX);
void remove_psk_entry(CTX);
void list_saved_psks(CTX);

void* heap_alloc(CTX, uint size);

char* fmt_ies_line(char* p, char* e, attr at, CTX);
char* fmt_ssid(char* p, char* e, byte* ssid, int slen);
int can_use_ap(CTX, attr at);

void read_config(CTX);
int check_entry(CTX, byte* ssid, int slen);
