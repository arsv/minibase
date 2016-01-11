#include <bits/types.h>

/* These are always aliased to fmt*32 or fmt*64 depending
   on native arch integer length. */

char* fmtlong(char* buf, char* end, long num);
char* fmtulong(char* buf, char* end, unsigned long num);
