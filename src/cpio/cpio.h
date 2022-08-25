#include <bits/types.h>

#define MAXDEPTH 15

struct bufout;

struct header {
	char magic[6];
	char ino[8];
	char mode[8];
	char uid[8];
	char gid[8];
	char nlink[8];
	char mtime[8];
	char filesize[8];
	char maj[8];
	char min[8];
	char rmaj[8];
	char rmin[8];
	char namesize[8];
	char checksum[8];
	char name[];
} __attribute__((packed));

struct cpio {
	int fd;
	char* name;

	off_t off;
	off_t rec;
};

struct heap {
	void* brk;
	void* ptr;
	void* end;
};

struct dent {
	void* buf;
	int len;
};

/* archive entry being packed */
struct entry {
	char* name;
	uint nlen;

	char* path;
	uint plen;

	uint size;
};

struct hwin {
	void* head;

	int hptr;
	int hend;
	int hlen;
};

struct htmp {
	void* buf;
	void* ptr;
	void* end;
	uint size;
};

struct list {
	int fd;
	char* name;

	void* buf;
	int len;

	int line;

	char* lp;
	char* ls;
	char* le;
};

struct top {
	int argc;
	int argi;
	char** argv;

	struct bufout* bo;
	struct cpio cpio;
	struct heap heap;
	struct hwin hwin;
	struct dent dent;
	struct entry entry;
	struct list list;
	struct htmp htmp;

	int at;
	char* dir;
	int depth;

	char* pref;
	char* rest;
	int plen;

	int null;
	int skip;
};

#define CTX struct top* ctx

static inline int align4(int x) { return (x + 3) & ~3; }

void fatal(CTX, char* msg) noreturn;
void failx(CTX, char* name, int ret) noreturn;
void failz(CTX, char* name, int ret) noreturn;

void cmd_create(CTX);
void cmd_extract(CTX);
void cmd_pack(CTX);
void cmd_list(CTX);

char* shift(CTX);
void no_more_arguments(CTX);
int got_more_arguments(CTX);

void heap_init(CTX, int size);
void* heap_alloc(CTX, int size);
void* heap_point(CTX);
void heap_reset(CTX, void* ptr);
void heap_extend(CTX, int size);

void open_cpio_file(CTX, char* name);
void make_cpio_file(CTX, char* name);
void open_base_dir(CTX, char* name);
void make_base_dir(CTX, char* name);

void put_pref(CTX);
void put_file(CTX, uint mode);
void put_symlink(CTX);
void put_trailer(CTX);
void put_immlink(CTX);

void reset_entry(CTX);
