#include <bits/types.h>

struct mbuf {
	char* buf;
	uint len;
};

struct rfn {
	int at;
	char* dir;
	char* name;
};

struct dev {
	char* path;

	char* devname;
	char* basename;
	char* subsystem;

	int devlen;
	int baselen;
	int subsyslen;
};

struct top {
	int udev;
	char** envp;
	int opts;

	int fd;  /* of a running modprobe -p process */
	int pid;

	struct mbuf config;
	struct mbuf passwd;
	struct mbuf group;
};

#define CTX struct top* ctx
#define FN struct rfn* fn
#define AT(dd) dd->at, dd->name
#define MD struct dev* md
#define MB struct mbuf* mb

void open_modprobe(CTX);
void stop_modprobe(CTX);
void modprobe(CTX, char* alias);

void trychown(CTX, char* subsystem, char* devname);

