#include <bits/types.h>

#define _format(n,m) __attribute__((format(printf,n,m)))

int printf(const char* fmt, ...) _format(1,2);
int tracef(const char* fmt, ...) _format(1,2);
int snprintf(char* buf, ulong len, const char* fmt, ...) _format(3,4);

#undef _format
