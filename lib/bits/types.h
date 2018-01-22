#ifndef __TYPES_H__
#define __TYPES_H__

#if __SIZEOF_LONG__ == 8
# define BITS 64
#elif __SIZEOF_LONG__ == 4
# define BITS 32
#else
# error unexpected word size
#endif

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
# define BIGENDIAN
#elif __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
# error unexpected byte order
#endif

typedef unsigned char byte;
typedef unsigned short ushort;
typedef unsigned uint;
typedef unsigned long ulong;

typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef signed short int16_t;
typedef unsigned short uint16_t;
typedef signed int int32_t;
typedef unsigned int uint32_t;

#if BITS == 64
typedef signed long int64_t;
typedef unsigned long uint64_t;
#else
typedef signed long long int64_t;
typedef unsigned long long uint64_t;
#endif

typedef int uid_t;
typedef int gid_t;
typedef int pid_t;

typedef long time_t;
typedef unsigned long size_t;
typedef int64_t off_t;

/* Let's take a moment to realize the significance of the two lines above.
   Size of stuff in memory is always unsigned, while size of stuff on disk
   is signed. This *will* trigger any strict signed-unsigned checks in a way
   that *cannot* be avoided. Ad-hoc casts to fix the warnings will only make
   the situation worse: 32 < 63 on 32-bit arches but 64 > 63 on 64-bit arches.

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
