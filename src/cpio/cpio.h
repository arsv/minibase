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
} packed;

struct top {
	int argc;
	int argi;
	char** argv;

	int fd;      /* the .cpio file being worked on */

	void* brk;   /* heap pointers */
	void* ptr;
	void* end;

	struct bufout* bo;

	int at;      /* fd of the host directory */
	char* dir;

	char* pref;
	int plen;

	void* head;
	int hptr;
	int hend;
	int hlen;
	int skip;

	int null;

	off_t off;
	off_t rec;

	void* dirbuf;
	int dirlen;

	int depth;
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
void heap_reset(CTX, void* ptr);
void heap_extend(CTX, int size);

void open_cpio_file(CTX, char* name);
void make_cpio_file(CTX, char* name);
void open_base_dir(CTX, char* name);
void make_base_dir(CTX, char* name);

void put_pref(CTX);
void put_file(CTX, char* path, char* name, uint size, int mode);
void put_link(CTX, char* path, char* name, uint size);
void put_trailer(CTX);
void put_immlink(CTX, char* name, int nlen, char* target, int size);
