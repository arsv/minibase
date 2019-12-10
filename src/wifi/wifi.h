#include <output.h>

#define AP_BADSSID 0
#define AP_NOCRYPT 1
#define AP_CANCONN 2

struct ucattr;

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

	void* brk;
	void* ptr;
	void* end;

	uint slen;
	byte ssid[32];
	byte psk[32];

	int fd;

	struct bufout bo;
	char rxbuf[128];

	int showbss;
	int unsaved;

	struct config cfg;

	struct ucattr** scans;
	int count;
};

#define CTX struct top* ctx __attribute__((unused))
#define MSG struct ucattr* msg
#define AT struct ucattr* at

void* heap_alloc(CTX, int size);

void find_wifi_device(char name[32]);
int get_ifindex(char name[32]);

void dump_status(CTX, MSG);
void dump_scan_list(CTX);

void warn_sta(CTX, char* text, MSG);
void warn_bss(CTX, char* text, MSG);

int load_saved_psk(CTX);
void ask_passphrase(CTX);
void maybe_store_psk(CTX);
void remove_psk_entry(CTX);
void list_saved_psks(CTX);

void fetch_scan_list(CTX);

char* fmt_ies_line(char* p, char* e, struct ucattr* at, CTX);
char* fmt_ssid(char* p, char* e, byte* ssid, int slen);
int can_use_ap(CTX, struct ucattr* ies);

void read_config(CTX);
int check_entry(CTX, byte* ssid, int slen);

void dump_status(CTX, MSG);
void dump_scan_list(CTX);
