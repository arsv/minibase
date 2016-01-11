#ifndef __SYSCALL2_H__
#define __SYSCALL2_H__

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

#endif
