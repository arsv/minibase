#include <cdefs.h>
#include <bits/types.h>

#define MAXDEPTH 31

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
#define BIT_MARK (1<<10)
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

	/* heap pointers */
	void* brk;
	void* ptr;
	void* end;

	/* package description */
	char* root; /* "/" but could be "/mnt/newroot" or something */
	char* repo; /* NULL or something glike "musl-arm" */
	char* name; /* "binutils" */

	char* pref; /* NULL or something like "/opt" */

	int rootfd; /* fd for the root dir above */
	int nullfd; /* fd for /dev/null */

	/* the .pac file being worked on */
	char* pac;  /* "path/to/binutils-1.11.pac" */
	int pacfd;  /* fd of the above */

	/* corresponding .pkg file */
	char* pkg;  /* "/var/mpkg/binutils.pac" */
	int pkgfd;  /* fd of the above */

	/* pac index and whatever data might have been read with it */
	void* head; /* a page or more read from the start of the .pac file */
	uint hoff;  /* offset of the file index (5..7, skipping "PAC?sss") */
	uint hlen;  /* length of the file index */
	/* also used for the .pkg file during removal, same logic there */

	/* rebuilt tree */
	struct node* index; /* contiguous array */
	uint nodes; /* number of elements in index[] */

	/* mmaped config file */
	void* cbrk;
	uint clen;

	/* In several places this tool has to walk directory tree. */
	int at;    /* fd of the current directory */
	int depth; /* depth of the current directory */
	int level; /* depth of current index entry */
	char* path[MAXDEPTH]; /* stack of directory names */
	int pfds[MAXDEPTH]; /* path fds, stack of saved `at` values */

	int fail; /* delayed-failure flag, see warnx() */
};

#define CTX struct top* ctx

char* shift(CTX);
void no_more_arguments(CTX);
int args_left(CTX);

void warnx(CTX, const char* msg, char* name, int err);
void failx(CTX, const char* msg, char* name, int err) noreturn;
void failz(CTX, const char* msg, char* name, int err) noreturn;

void cmd_deploy(CTX);
void cmd_remove(CTX);
void cmd_list(CTX);

void heap_init(CTX, int size);
void* alloc_tight(CTX, int size);
void* alloc_exact(CTX, int size);
void* alloc_align(CTX, int size);

void load_pacfile(CTX);
void load_config(CTX);
void check_config(CTX);

void parse_pkgname(CTX, char* name);
void setup_root(CTX, int fromarg);
void take_package_arg(CTX);
void take_pacfile_arg(CTX);

void check_filedb(CTX);
void write_filedb(CTX);
void load_filedb(CTX);
void unlink_filedb(CTX);

void setup_prefix(CTX, char* path);

char* root_adjust(char* name);

char* pkg_name(CTX);
char* pac_name(CTX);

char* reg_name(CTX);
char* reg_repo(CTX);

void need_zero_depth(CTX);
