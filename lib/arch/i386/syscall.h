#ifndef __SYSCALL_H__
#define __SYSCALL_H__

#include "bits/syscall.h"
#include "bits/errno.h"

/* x86 syscalls use 6 out of 7 available register to pass their arguments.
   It's not that hard to do in assembly even with 6 arguments, but gcc
   register allocator tends to bail out, especially on higher -O levels,
   claiming it cannot satisfy register constraints.

   So to avoid troubles, inline assembly is only used up to four arguments,
   and above that we just let gcc put the arguments into stack and sort them
   out in assembly. */

extern long syscall(long nr, long a, long b, long c, long d, long e, long f);

inline static long syscall0(int nr)
{
	register long eax asm("eax") = nr;

	asm volatile (
		"int $0x80"
	: "+r"(eax)
	: "0"(eax)
	: "memory");

	return eax;
}

inline static long syscall1(int nr, long a)
{
	register long eax asm("eax") = nr;
	register long ebx asm("ebx") = a;

	asm volatile (
		"int $0x80"
	: "+r"(eax)
	: "0"(eax), "r"(ebx)
	: "memory");

	return eax;
}

inline static long syscall2(int nr, long a, long b)
{
	register long eax asm("eax") = nr;
	register long ebx asm("ebx") = a;
	register long ecx asm("ecx") = b;

	asm volatile (
		"int $0x80"
	: "+r"(eax)
	: "0"(eax), "r"(ebx), "r"(ecx)
	: "memory");

	return eax;
}

inline static long syscall3(int nr, long a, long b, long c)
{
	register long eax asm("eax") = nr;
	register long ebx asm("ebx") = a;
	register long ecx asm("ecx") = b;
	register long edx asm("edx") = c;

	asm volatile (
		"int $0x80"
	: "+r"(eax)
	: "0"(eax), "r"(ebx), "r"(ecx), "r"(edx)
	: "memory");

	return eax;
}

inline static long syscall4(int nr, long a, long b, long c, long d)
{
	register long eax asm("eax") = nr;
	register long ebx asm("ebx") = a;
	register long ecx asm("ecx") = b;
	register long edx asm("edx") = c;
	register long esi asm("esi") = d;

	asm volatile (
		"int $0x80"
	: "+r"(eax)
	: "0"(eax), "r"(ebx), "r"(ecx), "r"(edx), "r"(esi)
	: "memory");

	return eax;
}

inline static long syscall5(int nr, long a, long b, long c, long d, long e)
{
	return syscall(nr, a, b, c, d, e, 0);
}

inline static long syscall6(int nr, long a, long b, long c, long d, long e, long f)
{
	return syscall(nr, a, b, c, d, e, f);
}

#endif
