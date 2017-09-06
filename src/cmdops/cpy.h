struct top {
	int argc;
	char** argv;
	int argi;
	int opts;

	char* buf;
	long len;

	int move;
	int newc;
	int user;

	int uid;
	int gid;
};

struct atfd {
	int at;
	char* dir;
	char* name;
	int fd;
};

struct atdir {
	int at;
	char* dir;
};

struct cct {
	struct top* top;
	struct atdir dst;
	struct atdir src;
	int nosf;
};

#define CTX struct top* ctx
#define SRC struct atfd* src
#define DST struct atfd* dst
#define CCT struct cct* cct

#define noreturn __attribute__((noreturn))

void failat(const char* msg, char* dir, char* name, int err) noreturn;
void warnat(const char* msg, char* dir, char* name, int err);

void runrec(CCT, char* dstname, char* srcname, int type);
void copyfile(CCT, char* dstname, char* srcname, struct stat* st);
void apply_props(CCT, char* dir, char* name, int fd, struct stat* st);
