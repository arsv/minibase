#ifndef __SYSCALL_H__
#define __SYSCALL_H__

#include "bits/syscall.h"
#include "bits/errno.h"

inline static long syscall0(int nr)
{
	register long r7 asm("a7") = nr;
	register long r0 asm("a0");

	asm volatile ("ecall" : "=r"(r0) : "r"(r7));
	
	return r0;
}

inline static long syscall1(int nr, long a1)
{
	register long r7 asm("a7") = nr;
	register long r0 asm("a0") = a1;

	asm volatile ("ecall" : "=r"(r0) : "r"(r7), "r"(r0));
	
	return r0;
}

inline static long syscall2(int nr, long a1, long a2)
{
	register long r7 asm("a7") = nr;
	register long r0 asm("a0") = a1;
	register long r1 asm("a1") = a2;

	asm volatile ("ecall" : "=r"(r0)
		: "r"(r7), "r"(r0), "r"(r1));
	
	return r0;
}

inline static long syscall3(int nr, long a1, long a2, long a3)
{
	register long r7 asm("a7") = nr;
	register long r0 asm("a0") = a1;
	register long r1 asm("a1") = a2;
	register long r2 asm("a2") = a3;

	asm volatile ("ecall" : "=r"(r0)
		: "r"(r7), "r"(r0), "r"(r1), "r"(r2));
	
	return r0;
}

inline static long syscall4(int nr, long a1, long a2, long a3, long a4)
{
	register long r7 asm("a7") = nr;
	register long r0 asm("a0") = a1;
	register long r1 asm("a1") = a2;
	register long r2 asm("a2") = a3;
	register long r3 asm("a3") = a4;

	asm volatile ("ecall" : "=r"(r0)
		: "r"(r7), "r"(r0), "r"(r1), "r"(r2), "r"(r3));
	
	return r0;
}

inline static long syscall5(int nr, long a1, long a2, long a3, long a4, long a5)
{
	register long r7 asm("a7") = nr;
	register long r0 asm("a0") = a1;
	register long r1 asm("a1") = a2;
	register long r2 asm("a2") = a3;
	register long r3 asm("a3") = a4;
	register long r4 asm("a4") = a5;

	asm volatile ("ecall" : "=r"(r0)
		: "r"(r7), "r"(r0), "r"(r1), "r"(r2), "r"(r3), "r"(r4));
	
	return r0;
}

inline static long syscall6(int nr, long a1, long a2, long a3, long a4,
		long a5, long a6)
{
	register long r7 asm("a7") = nr;
	register long r0 asm("a0") = a1;
	register long r1 asm("a1") = a2;
	register long r2 asm("a2") = a3;
	register long r3 asm("a3") = a4;
	register long r4 asm("a4") = a5;
	register long r5 asm("a5") = a6;

	asm volatile ("ecall" : "=r"(r0)
	: "r"(r7), "r"(r0), "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5));
	
	return r0;
}

#endif
