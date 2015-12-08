#ifndef __ALLOCA_H__
#define __ALLOCA_H__

#ifdef __GNUC__
#define alloca(n) __builtin_alloca(n)
#else
#error alloca() is not supported on this compiler/arch
#endif

#endif
