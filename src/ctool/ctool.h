#include <cdefs.h>
#include <bits/types.h>

#define MAXDEPTH 8

/* Bits in the leading byte (tag) of each entry from .pac index */
#define TAG_DIR  (1<<7)

#define TAG_TYPE (3<<2) /* mask */
#define TAG_FILE (1<<2)
#define TAG_EXEC (2<<2)
#define TAG_LINK (3<<2)

#define TAG_SIZE (3<<0) /* mask */
#define TAG_SZ_1 (0<<0)
#define TAG_SZ_2 (1<<0)
#define TAG_SZ_3 (2<<0)
#define TAG_SZ_4 (3<<0)

#define TAG_DEPTH 0x7F /* mask */

/* Additional bits we may set in struct node, along the TAG_ bits above. */
#define BIT_NEED (1<<11)
#define BIT_EXST (1<<12)

struct node {
	char* name;
	uint size;   /* decoded and ready to use */
	uint bits;   /* TAG and BIT constants above */
};

struct top {
	int argc;
	int argi;
	char** argv;
	char** envp;

	/* heap pointers */
	void* brk;
	void* ptr;
	void* end;

	char* libname; /* "netlink" */
	char* pacpath;  /* "rep/netlink.pac.gz" */
	int pacfd;      /* fd of the above */
	char* fdbpath;  /* "pkg/netlink.list" */
	int fdbfd;      /* fd of the above */

	void* head;  /* a page or more read from the start of the .pac file */
	uint hoff;   /* index offset into head */
	uint hlen;   /* length of the data in head */

	/* rebuilt tree */
	struct node* index; /* contiguous array */
	uint nodes; /* number of elements in index[] */

	/* transfer buffer */
	void* databuf;
	uint datasize;

	/* In several places this tool has to walk directory tree. */
	int at;    /* fd of the current directory */
	int depth; /* depth of the current directory */
	int level; /* depth of current index entry */
	char* path[MAXDEPTH]; /* stack of directory names */
	int pfds[MAXDEPTH]; /* path fds, stack of saved `at` values */

	int fail; /* delayed-failure flag, see warnx() */
};

#define CTX struct top* ctx __unused

char* shift(CTX);
void no_more_arguments(CTX);
int args_left(CTX);

void warnx(CTX, const char* msg, char* name, int err);
void failx(CTX, const char* msg, char* name, int err) noreturn;
void failz(CTX, const char* msg, char* name, int err) noreturn;

void cmd_use(CTX);
void cmd_repo(CTX);
void cmd_reset(CTX);
void cmd_rebin(CTX);

void cmd_add(CTX);
void cmd_del(CTX);

void cmd_reset(CTX);
void cmd_clear(CTX);

void heap_init(CTX, int size);
void* alloc_align(CTX, int size);
void* alloc_exact(CTX, int size);

void locate_package(CTX, char* name);
void check_filedb(CTX);
void write_filedb(CTX);

void parse_pkgname(CTX, char* name);
void check_workdir(CTX);
void take_package_arg(CTX);
void take_pacfile_arg(CTX);

void setup_prefix(CTX, char* path);

void need_zero_depth(CTX);
void take_libname_arg(CTX);

int looks_like_path(char* name);
void load_index(CTX);

void heap_reset(CTX, void* ptr);

void remove_package(CTX, char* name);
