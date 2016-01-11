#ifndef __SYSCALL6_H__
#define __SYSCALL6_H__

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
		: "rcx", "r11");

	return r0;
};

#endif
