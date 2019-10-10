#ifndef __CDEFS_H__
#define __CDEFS_H__

#include <bits/errno.h>
#include <bits/types.h>

#define __unused __attribute__((unused))
#define __packed __attribute__((packed))
#define noreturn __attribute__((noreturn))

#define unused(x) (void)x

#define alloca(n) __builtin_alloca(n)
#define offsetof(t, f) __builtin_offsetof(t, f)

#define ARRAY_SIZE(a) (sizeof(a)/sizeof(*a))
#define ARRAY_END(a) (a + ARRAY_SIZE(a))

#define STDIN  0
#define STDOUT 1
#define STDERR 2

/* The most obnoxious constant around. *LOTS* of files need this
   and nothing more from the headers. */

#define NULL ((void*)0)

#endif
