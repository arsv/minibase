#ifndef __ERRTAG_H__
#define __ERRTAG_H__

#include <bits/errno.h>

extern const struct errcode {
	short code;
	char* name;
} errlist[];

#define ERRTAG const char errtag[]
#define ERRLIST const struct errcode errlist[]

extern ERRTAG;
extern ERRLIST;

#define REPORT(e) { e, #e }
#define RESTASNUMBERS { 0 }

#endif
