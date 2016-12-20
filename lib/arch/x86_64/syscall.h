#ifndef __SYSCALL_H__
#define __SYSCALL_H__

#include "bits/syscall.h"
#include "bits/errno.h"

inline static long syscall0(long nr)
{
	register long r0 asm("rax") = nr;

	asm volatile ("syscall" : "=r"(r0) : "r"(r0) : "rcx", "r11");

	return r0;
};

inline static long syscall1(long nr, long a1)
{
	register long r0 asm("rax") = nr;
	register long r1 asm("rdi") = a1;

	asm volatile ("syscall" : "=r"(r0) : "r"(r0), "r"(r1) : "rcx", "r11");

	return r0;
};

inline static long syscall2(long nr, long a1, long a2)
{
	register long r0 asm("rax") = nr;
	register long r1 asm("rdi") = a1;
	register long r2 asm("rsi") = a2;

	asm volatile ("syscall" : "=r"(r0)
		: "r"(r0), "r"(r1), "r"(r2)
		: "rcx", "r11");

	return r0;
};

inline static long syscall3(long nr, long a1, long a2, long a3)
{
	register long r0 asm("rax") = nr;
	register long r1 asm("rdi") = a1;
	register long r2 asm("rsi") = a2;
	register long r3 asm("rdx") = a3;

	asm volatile ("syscall" : "=r"(r0)
		: "r"(r0), "r"(r1), "r"(r2), "r"(r3)
		: "rcx", "r11", "memory");

	return r0;
};

inline static long syscall4(long nr, long a1, long a2, long a3, long a4)
{
	register long r0 asm("rax") = nr;
	register long r1 asm("rdi") = a1;
	register long r2 asm("rsi") = a2;
	register long r3 asm("rdx") = a3;
	register long r4 asm("r10") = a4;

	asm volatile ("syscall" : "=r"(r0)
		: "r"(r0), "r"(r1), "r"(r2), "r"(r3), "r"(r4)
		: "rcx", "r11", "memory");

	return r0;
};

inline static long syscall5(long nr, long a1, long a2, long a3, long a4, long a5)
{
	register long r0 asm("rax") = nr;
	register long r1 asm("rdi") = a1;
	register long r2 asm("rsi") = a2;
	register long r3 asm("rdx") = a3;
	register long r4 asm("r10") = a4;
	register long r5 asm("r8") = a5;

	asm volatile ("syscall" : "=r"(r0)
		: "r"(r0), "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5)
		: "rcx", "r11", "memory");

	return r0;
};

inline static long syscall6(long nr, long a1, long a2, long a3, long a4,
		long a5, long a6)
{
	register long r0 asm("rax") = nr;
	register long r1 asm("rdi") = a1;
	register long r2 asm("rsi") = a2;
	register long r3 asm("rdx") = a3;
	register long r4 asm("r10") = a4;
	register long r5 asm("r8") = a5;
	register long r6 asm("r9") = a6;

	asm volatile ("syscall" : "=r"(r0)
		: "r"(r0), "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5), "r"(r6)
		: "rcx", "r11", "memory");

	return r0;
};

#endif
