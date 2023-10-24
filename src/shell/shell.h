#include <bits/ioctl/tty.h>
#include <cdefs.h>

extern const char errtag[];

extern struct shell {
	int opts;
	char** envp;

	int state; /* of parsing */

	/* these index into argbuf */
	int argsep; /* start of current (incomplete) arg */
	int argptr; /* end of current (incomplete) arg */
	/* these index into args[] below */
	int argidx; /* arg index, for shift()ing */
	int argcnt; /* arg count */

	char* args[20];

	/* heap */
	void* brk;
	void* ptr;
	void* end;
} sh;

char* shift(void);
char* shift_arg(void);

int got_more_args(void);
int extra_arguments(void);

void run_command(void);

void init_heap(void);
void reset_heap(void);
void* heap_alloc(uint size);
void heap_reset(void* ptr);

void repl(char* msg, char* arg, int err);
void output(char* buf, int len);

//void output(char* str, int len);
//void outstr(char* str);

void cmd_echo(void);

void cmd_ls(void);
void cmd_la(void);
void cmd_ld(void);
void cmd_lf(void);
void cmd_lx(void);

void cmd_lh(void);
void cmd_lhf(void);
void cmd_lhd(void);

void cmd_stat(void);
void cmd_info(void);
void cmd_time(void);

void cmd_ps(void);
void cmd_kill(void);

void cmd_write(void);
