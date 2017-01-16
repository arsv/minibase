#include <bits/types.h>
#include <bits/signal.h>
#include <bits/ppoll.h>
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

	char* ring;
	short head;
	short tail;
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

extern struct svcmon gg;
extern struct svcrec recs[];
extern struct pollfd pfds[];

int reload(void);

struct svcrec* findrec(char* name);
struct svcrec* makerec(void);

void initpass(void);
void killpass(void);
void waitpoll(void);
void waitpids(void);

int setsignals(void);
void setctl(void);
void acceptctl(int sfd);
void parsecmd(char* cmd);
void wakeupin(int ttw);
void stopall(void);
void setpollfd(int i, int fd);
void bufoutput(int fd, int i);

struct svcrec* firstrec(void);
struct svcrec* nextrec(struct svcrec* rc);
int recindex(struct svcrec* rc);
void droprec(struct svcrec* rc);
void flushrec(struct svcrec* rc);

void report(char* msg, char* arg, int err);
void reprec(struct svcrec* rc, char* msg);

void setbrk(void);
char* alloc(int len);
void afree(void);
