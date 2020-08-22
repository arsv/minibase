#define DEVIDLEN 46
#define MODESIZE 16

struct top {
	int opts;
	int argc;
	int argi;
	char** argv;

	int fd;
	struct ucbuf uc;
	int connected;
	char txbuf[64];
	char rxbuf[512];

	int ifi;
	char* name;
	char mode[MODESIZE];
	char devid[DEVIDLEN];
};

struct ucattr;
typedef struct ucattr* attr;

#define CTX struct top* ctx __unused
#define MSG struct ucattr* msg __unused
#define AT struct ucattr* at __unused
#define UC (&ctx->uc)

void dump_status(CTX, MSG);

void identify_device(CTX);
void store_device_mode(CTX, char* mode);
void store_device_also(CTX, char* mode);
void clear_device_entry(CTX);
void load_device_mode(CTX, char* mode, int len);
