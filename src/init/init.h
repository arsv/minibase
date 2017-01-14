#include <bits/types.h>
#include <bits/signal.h>
#include "config.h"

#define S_SIGCHLD (1<<0)
#define S_WAITING (1<<1)
#define S_TERMREQ (1<<2)
#define S_CTRLREQ (1<<3)
#define S_REOPEN  (1<<4)
#define S_RELOAD  (1<<5)

#define P_DISABLED (1<<0)
#define P_SIGSTOP  (1<<1)
#define P_SIGTERM  (1<<2)
#define P_SIGKILL  (1<<3)
#define P_STALE    (1<<4)

struct initrec {
	int pid;
	uint8_t flags;
	char name[NAMELEN];
	time_t lastrun;
	time_t lastsig;
	int status;
};

struct init {
	int state;
	int timetowait;
	time_t passtime;
	char rbcode;

	int ctlfd;
	int outfd;

	int uid;

	char* brk;
	char* ptr;
	char* end;

	char* initdir;
	char** env;
};

extern struct init gg;

int reload(void);

struct initrec* findrec(char* name);
struct initrec* makerec(void);

void initpass(void);
void killpass(void);
void waitpoll(void);
void waitpids(void);

int setsignals(void);
void setinitctl(void);
void acceptctl(void);
void parsecmd(char* cmd);

int anyrunning(void);

struct initrec* firstrec(void);
struct initrec* nextrec(struct initrec* rc);
void droprec(struct initrec* rc);

void report(char* msg, char* arg, int err);
void reprec(struct initrec* rc, char* msg);

char* alloc(int len);
