#include <bits/types.h>
#include "config.h"

#define S_SIGCHLD (1<<0)
#define S_PASSREQ (1<<1)
#define S_TERMREQ (1<<2)
#define S_REOPEN  (1<<4)
#define S_RELOAD  (1<<5)
#define S_REBOOT  (1<<6)

#define P_DISABLED (1<<0)
#define P_SIGSTOP  (1<<1)
#define P_SIGTERM  (1<<2)
#define P_SIGKILL  (1<<3)
#define P_STALE    (1<<4)

struct svcrec {
	int pid;
	uint8_t flags;
	char name[NAMELEN];
	time_t lastrun;
	time_t lastsig;
	int status;
};

struct svcmon {
	int state;
	char rbcode;
	int outfd;
	int uid;

	char* dir;
	char** env;

	int nr;
};

struct ringbuf {
	char* buf;
	short ptr;
};

extern struct svcmon gg;

int reload(void);

struct svcrec* findrec(char* name);
struct svcrec* makerec(void);
struct svcrec* findpid(int pid);

void initpass(void);
void waitpoll(void);
void waitpids(void);

int setsignals(void);
void setctl(void);
void acceptctl(int sfd);
void wakeupin(int ttw);
void stopall(void);

void setctrlfd(int fd);
void setpollfd(struct svcrec* rc, int fd);
void flushring(struct svcrec* rc);
struct ringbuf* ringfor(struct svcrec* rc);

struct svcrec* firstrec(void);
struct svcrec* nextrec(struct svcrec* rc);
int recindex(struct svcrec* rc);
void droprec(struct svcrec* rc);

void report(char* msg, char* arg, int err);
void reprec(struct svcrec* rc, char* msg);

void setbrk(void);
char* alloc(int len);
void afree(void);
