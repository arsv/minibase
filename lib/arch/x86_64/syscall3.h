#ifndef __SYSCALL3_H__
#define __SYSCALL3_H__

inline static long syscall3(long nr, long a1, long a2, long a3)
{
	register long r0 asm("rax") = nr;
	register long r1 asm("rdi") = a1;
	register long r2 asm("rsi") = a2;
	register long r3 asm("rdx") = a3;

	asm volatile ("syscall" : "=r"(r0)
		: "r"(r0), "r"(r1), "r"(r2), "r"(r3)
		: "rcx", "r11");

	return r0;
};

#endif
