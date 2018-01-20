#include <bits/types.h>

struct mbuf {
	char* buf;
	uint len;
};

struct ubuf {
	char* buf;
	uint ptr;
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

#define BITSET(name,n) ulong name[(n+sizeof(long)-1)/sizeof(long)]

struct evbits {
	BITSET(ev,   32);
	BITSET(key, 256);
	BITSET(rel,  16);
	BITSET(abs,  64);
	BITSET(led,  10);
	BITSET(sw,   16);
};

#undef BITSET

struct top {
	int udev;
	char** envp;
	int opts;

	int fd;  /* of a running modprobe -p process */
	int pid;

	int startup;

	struct mbuf config;
	struct mbuf passwd;
	struct mbuf group;

	uint sep;
	uint ptr;
	char uevent[512+2];

	char saveid[16];
};

#define CTX struct top* ctx __unused
#define FN struct rfn* fn
#define AT(dd) dd->at, dd->name
#define MD struct dev* md
#define MB struct mbuf* mb

void open_modprobe(CTX);
void stop_modprobe(CTX);
void modprobe(CTX, char* alias);

void trychown(CTX, char* subsystem, char* devname);

void init_inputs(CTX);
void probe_input(CTX);
void clear_input(CTX);

char* getval(CTX, char* key);
