#ifndef __SYSCALL_H__
#define __SYSCALL_H__

#include "bits/syscall.h"
#include "bits/errno.h"

inline static long syscall0(int nr)
{
	register long x8 asm("x8") = nr;
	register long x0 asm("x0");

	asm volatile ("svc 0"
		: "=r"(x0)
		: "r"(x8)
		: "memory");

	return x0;
}

inline static long syscall1(int nr, long a1)
{
	register long x8 asm("x8") = nr;
	register long x0 asm("x0") = a1;

	asm volatile ("svc 0"
		: "=r"(x0)
		: "r"(x8), "r"(x0)
		: "memory");

	return x0;
}

inline static long syscall2(int nr, long a1, long a2)
{
	register long x8 asm("x8") = nr;
	register long x0 asm("x0") = a1;
	register long x1 asm("x1") = a2;

	asm volatile ("svc 0"
		: "=r"(x0)
		: "r"(x8), "r"(x0), "r"(x1)
		: "memory");

	return x0;
}

inline static long syscall3(int nr, long a1, long a2, long a3)
{
	register long x8 asm("x8") = nr;
	register long x0 asm("x0") = a1;
	register long x1 asm("x1") = a2;
	register long x2 asm("x2") = a3;

	asm volatile ("svc 0"
		: "=r"(x0)
		: "r"(x8), "r"(x0), "r"(x1), "r"(x2)
		: "memory");

	return x0;
}

inline static long syscall4(int nr, long a1, long a2, long a3, long a4)
{
	register long x8 asm("x8") = nr;
	register long x0 asm("x0") = a1;
	register long x1 asm("x1") = a2;
	register long x2 asm("x2") = a3;
	register long x3 asm("x3") = a4;

	asm volatile ("svc 0"
		: "=r"(x0)
		: "r"(x8), "r"(x0), "r"(x1), "r"(x2), "r"(x3)
		: "memory");

	return x0;
}

inline static long syscall5(int nr, long a1, long a2, long a3, long a4, long a5)
{
	register long x8 asm("x8") = nr;
	register long x0 asm("x0") = x0;
	register long x1 asm("x1") = x1;
	register long x2 asm("x2") = x2;
	register long x3 asm("x3") = x3;
	register long x4 asm("x4") = x4;

	asm volatile ("svc 0"
		: "=r"(x0)
		: "r"(x8), "r"(x0), "r"(x1), "r"(x2), "r"(x3), "r"(x4)
		: "memory");

	return x0;
}

inline static long syscall6(int nr, long a1, long a2, long a3, long a4,
		long a5, long a6)
{
	register long x8 asm("r7") = nr;
	register long x0 asm("r0") = a1;
	register long x1 asm("r1") = a2;
	register long x2 asm("r2") = a3;
	register long x3 asm("r3") = a4;
	register long x4 asm("r4") = a5;
	register long x5 asm("r5") = a6;

	asm volatile ("svc 0"
		: "=r"(x0)
		: "r"(x8), "r"(x0), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5)
		: "memory");

	return x0;
}

#endif
