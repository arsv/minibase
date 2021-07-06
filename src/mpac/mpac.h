struct bufout;

#define TAG_DIR  (1<<7)

#define TAG_TYPE (3<<2) /* mask */
#define TAG_FILE (1<<2)
#define TAG_EXEC (2<<2)
#define TAG_LINK (3<<2)

#define TAG_SIZE (3<<0) /* mask */

#define TAG_DEPTH 0x7F /* mask */

#define MAXDEPTH 32

struct top {
	int argc;
	int argi;
	char** argv;
	char** envp;

	/* the .pac file being worked on */
	int fd;

	/* the top directory being packed or unpacked to */
	char* root;

	/* heap pointers */
	void* brk;
	void* ptr;
	void* end;

	/* load-specific stuff (index parsing) */
	void* head;
	int hoff;
	int hlen;

	void* iptr;
	void* iend;

	char* name;
	uint nlen;
	uint size;

	/* pack-specific fields */
	void* dirbuf;
	int dirlen;

	uint hsize;
	void* idx; /* last index */

	char* src;

	struct bufout* bo;

	int at; /* fd of the host directory */
	int depth; /* of the host directory */

	char* path[MAXDEPTH];
	int pfds[MAXDEPTH];

	void* databuf;
	uint datasize;
};

#define CTX struct top* ctx

void quit(CTX, const char* msg, char* arg, int ret) noreturn;

void cmd_create(CTX);
void cmd_extract(CTX);
void cmd_pack(CTX);
void cmd_list(CTX);
void cmd_dump(CTX);

char* shift(CTX);
void no_more_arguments(CTX);

void check_pac_ext(char* name);
void check_list_ext(char* name);

void heap_init(CTX, int size);
void* heap_alloc(CTX, int size);
void heap_reset(CTX, void* ptr);

void open_pacfile(CTX, char* name);
void load_index(CTX);

int next_entry(CTX);

void failx(CTX, const char* msg, char* name, int ret) noreturn;

void need_zero_depth(CTX);
