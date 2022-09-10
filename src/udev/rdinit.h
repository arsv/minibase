
#define FL_DONE     (1<<0)
#define FL_FAILURE  (1<<1)
#define FL_BAD_FDS  (1<<2)
#define FL_NEWROOT  (1<<3)

struct pollfd;

struct top {
	//char** argv;
	char** envp;
	char** next;
	int argc;
	int flags;
	int failure;

	int devfd;  /* udev socket, in */
	int sigfd;  /* signalfd, in */
	int modfd;  /* modpipe, out */

	int runpid; /* primary child script */
	int modpid; /* modpipe */

	uint64_t olddev;
	uint64_t newdev;

	struct pollfd pfds[2];

	char uevent[1024+2];
};

#define CTX struct top* ctx

void locate_devices(CTX);
void clear_initramfs(CTX);

noreturn void abort(CTX, char* msg, char* arg);
