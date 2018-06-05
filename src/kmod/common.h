#include <bits/types.h>
#include <bits/elf.h>
#include <cdefs.h>

struct top;

#define CTX struct top* ctx __unused
#define MOD struct kmod* mod

struct mbuf {
	void* buf;
	uint len;
	uint full;
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

int mmap_whole(CTX, struct mbuf* mb, char* name);
int map_lunzip(CTX, struct mbuf* mb, char* name);
int decompress(CTX, struct mbuf* mb, char* name, char** cmd);
void munmap_buf(struct mbuf* mb);

int find_modinfo(CTX, struct kmod* mod, struct mbuf* mb, char* name);

extern int error(CTX, const char* msg, char* arg, int err);
extern char** environ(CTX);
