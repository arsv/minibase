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

/* In Linux, size_t is always unsigned while off_t is always *signed*.
   This *will* trigger strict signed-unsigned checks in a way that *cannot*
   be avoided any time size in memory is compared to size on disk.
   Ad-hoc casts to fix the warnings will only make the situation worse:
   32 < 63 on 32-bit arches but 64 > 63 on 64-bit arches.

   The following is a reasonably safe way to avoid signed-unsigned compare
   warnings. It will not help with assignments however, those must be handled
   *very* carefully.

   The proper way to fix this is to make file sizes unsigned and change seek
   modes to { SEEK_SET, SEEK_FWD, SEEK_REV, SEEK_END }, all taking unsigned
   offsets. Too bad the wrong decision is already burned into the syscall
   interface.

   Return: -1 if msz < fsz, +1 if msz > fsz. */

inline static int mem_off_cmp(size_t msz, off_t fsz)
{
	if(fsz < 0)
		return 1;

#if BITS == 32 /* sizeof(msz) < sizeof(fsz) */
	off_t smsz = (off_t)msz;

	if(smsz < fsz)
		return -1;
	if(smsz > fsz)
		return 1;
#else /* sizeof(msz) == sizeof(fsz) */
	size_t ufsz = (size_t)fsz;

	if(msz < ufsz)
		return -1;
	if(msz > ufsz)
		return 1;
#endif

	return 0;
}

#endif
