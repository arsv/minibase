struct top {
	int argc;
	char** argv;
	int argi;
	int opts;

	int dryrun;
	int errors;

	char* buf;
	long len;

	int move;
	int newc;
	int user;

	int uid;
	int gid;

	void* brk;
	void* ptr;
	void* end;
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
