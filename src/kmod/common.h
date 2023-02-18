#include <bits/types.h>
#include <bits/elf.h>
#include <cdefs.h>

/* for mmap_whole */
#define REQ 0
#define OPT 1

struct top;

#define CTX struct top* ctx __unused
#define MOD struct kmod* mod

struct mbuf {
	void* buf;
	uint len;    /* content length */
	uint full;   /* mmaped length */
};

struct line {
	char* ptr;
	char* sep;
	char* val;
	char* end;
};

struct kmod {
	char* name;

	void* buf;
	uint len;

	int elf64;
	int elfxe;

	uint64_t shoff;
	uint16_t shnum;
	uint16_t shentsize;
	uint16_t shstrndx;

	int strings_off;
	int strings_len;

	uint modinfo_off;
	uint modinfo_len;
};

int load_module(CTX, struct mbuf* mb, char* path);

int mbuf_dirty(struct mbuf* mb);
int mmap_whole(CTX, struct mbuf* mb, char* name, int optional);
int map_lunzip(CTX, struct mbuf* mb, char* name);
int decompress(CTX, struct mbuf* mb, char* name, char** cmd);
void munmap_buf(struct mbuf* mb);

int find_modinfo(CTX, struct kmod* mod, struct mbuf* mb, char* name);

extern int error(CTX, const char* msg, char* arg, int err);
extern char** environ(CTX);

static inline int isspace(int c)
{
	return (c == ' ' || c == '\t');
}

int locate_dep_line(struct mbuf* mb, struct line* ln, char* mod);
int locate_opt_line(struct mbuf* mb, struct line* ln, char* mod);
int locate_alias_line(struct mbuf* mb, struct line* ln, char* mod);
int locate_blist_line(struct mbuf* mb, struct line* ln, char* mod);
int locate_built_line(struct mbuf* mb, struct line* ln, char* mod);
