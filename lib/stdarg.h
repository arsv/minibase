#ifndef STDARG_H
#define STDARG_H

#ifdef va_end
/* it's already defined */
#elif (__GNUC__ > 2) || ((__GNUC__ == 2) && (__GNUC_MINOR__ >= 96))
typedef __builtin_va_list va_list;
#define va_start(v,l)	__builtin_va_start((v),(l))
#define va_end		__builtin_va_end
#define va_arg		__builtin_va_arg
#define __va_copy(d,s)	__builtin_va_copy((d),(s))
#else
#error No stdarg.h found for you target architecture
#endif

#endif
