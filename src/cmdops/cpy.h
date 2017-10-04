#define OPTS "nthomuqyv"

#define OPT_n (1<<0)     /* new copy, no overwriting */
#define OPT_t (1<<1)     /* copy to */
#define OPT_h (1<<2)     /* copy here */
#define OPT_o (1<<3)     /* copy over, do overwrite */
#define OPT_m (1<<4)     /* move */
#define OPT_u (1<<5)     /* keep file ownership */
#define OPT_q (1<<6)     /* query, dry run only */
#define OPT_y (1<<7)     /* yolo mode, skip dry run */
#define OPT_v (1<<8)     /* verbose */

#define DRY (1<<16)    /* dry run */

struct top {
	int argc;
	char** argv;
	int argi;
	int opts;

	char* buf;
	size_t len;

	int uid;
	int gid;

	void* brk;
	void* ptr;
	void* end;

	int errors;
};

struct atf {
	int at;
	char* dir;
	char* name;
	int fd;
};

struct cct {
	struct top* top;
	struct atf dst;
	struct atf src;
	struct stat st;
	int wrchecked;
	int nosendfile;
	int dstatdup;
};

struct link {
	int len;
	int at;
	uint64_t sdev;
	uint64_t sino;
	uint64_t ddev;
	uint64_t dino;
	char name[];
};

#define CTX struct top* ctx
#define CCT struct cct* cct
#define AT(dd) dd->at, dd->name

#define noreturn __attribute__((noreturn))

void warnat(const char* msg, struct atf* dd, int err);
void failat(const char* msg, struct atf* dd, int err) noreturn;

void run(CTX, CCT, char* dst, char* src);

void copyfile(CCT);
void trychown(CCT);

void note_ino(CCT);

int link_dst(CCT);
