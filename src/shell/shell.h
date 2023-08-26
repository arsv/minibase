#include <bits/ioctl/tty.h>
#include <cdefs.h>

extern int argcount;
extern char* args[20];

extern char tmpbuf[1024];
extern char outbuf[2048];

char* shift(void);
void output(char* str, int len);
void outstr(char* str);

void run_command(void);
