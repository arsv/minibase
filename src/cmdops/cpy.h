struct top {
	int argc;
	char** argv;
	int argi;
	int opts;

	char* buf;
	long len;

	int move;
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

void transfer(CCT, DST, SRC, uint64_t* size);

#define noreturn __attribute__((noreturn))

void runrec(CCT, char* dstname, char* srcname, int type);
void failat(const char* msg, char* dir, char* name, int err) noreturn;
