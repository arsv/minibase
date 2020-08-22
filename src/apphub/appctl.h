struct top {
	int opts;
	int argc;
	int argi;
	char** argv;
	char** environ;

	int fd;
	int connected;
	char txbuf[64];
	char rxbuf[512];
};

struct ucattr;
typedef struct ucattr* attr;

#define CTX struct top* ctx __unused
#define MSG struct ucmsg* msg __unused
#define AT struct ucattr* at __unused
#define UC struct ucbuf* uc
