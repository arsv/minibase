#include <bits/types.h>
#include <bits/time.h>
#include <bits/ioctl/tty.h>
#include <crypto/scrypt.h>
#include <crypto/aes128.h>
#include <cdefs.h>

#define NUMKEYS 16
#define KEYSIZE 32
#define HDRSIZE 16
#define SALTLEN 8
#define MAXPASS 128
#define PADFILE 16
#define MAXFILE 528 /* HDRSIZE + NUMKEYS*KEYSIZE */
#define FILEBUF 544 /* HDRSIZE + NUMKEYS*KEYSIZE + PADFILE */

#define ST_INPUT     0
#define ST_INVALID   1
#define ST_HASHING   2

#define IS_REG  0
#define IS_ESC  1
#define IS_CSI  2

struct part {
	short keyidx;
	char* label;
	uint64_t size;
	uint64_t rdev;
	uint dmidx;
};

struct top {
	int sigfd;
	int mapfd;

	int rows;
	int cols;
	struct termios tio;
	int termactive;

	char pass[MAXPASS];
	int plen;
	int showpass;
	int needwait;

	int state;
	int keyst;

	int incol;
	int inrow;
	int inlen;
	int psptr;

	struct timespec ts;
	struct scrypt sc;

	int nparts;
	struct part parts[NUMKEYS];

	int nkeys;
	char* keyfile;
	uint keysize;
	byte keydata[FILEBUF];
	byte keycopy[FILEBUF];
};

#define CTX struct top* ctx __unused

void quit(CTX, const char* msg, char* arg, int err) noreturn;

void start_terminal(CTX);
void handle_input(CTX, char* buf, int len);
void handle_sigwinch(CTX);
void handle_user_end(CTX);
void handle_timeout(CTX);

void load_key_data(CTX, char* name);
void alloc_scrypt(CTX);
int unwrap_keydata(CTX);
void init_mapper(CTX);
void decrypt_parts(CTX);
