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

#endif
