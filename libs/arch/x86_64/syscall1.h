#ifndef __SYSCALL1_H__
#define __SYSCALL1_H__

inline static long syscall1(long nr, long a1)
{
	register long r0 asm("rax") = nr;
	register long r1 asm("rdi") = a1;

	asm volatile ("syscall" : "=r"(r0) : "r"(r0), "r"(r1) : "rcx", "r11");

	return r0;
};

#endif
