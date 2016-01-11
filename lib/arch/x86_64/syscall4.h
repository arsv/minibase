#ifndef __SYSCALL4_H__
#define __SYSCALL4_H__

inline static long syscall4(long nr, long a1, long a2, long a3, long a4)
{
	register long r0 asm("rax") = nr;
	register long r1 asm("rdi") = a1;
	register long r2 asm("rsi") = a2;
	register long r3 asm("rdx") = a3;
	register long r4 asm("r10") = a4;

	asm volatile ("syscall" : "=r"(r0)
		: "r"(r0), "r"(r1), "r"(r2), "r"(r3), "r"(r4)
		: "rcx", "r11");

	return r0;
};

#endif
