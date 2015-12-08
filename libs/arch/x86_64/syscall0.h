#ifndef __SYSCALL0_H__
#define __SYSCALL0_H__

inline static long syscall0(long nr)
{
	register long r0 asm("rax") = nr;

	asm volatile ("syscall" : "=r"(r0) : "r"(r0) : "rcx", "r11");

	return r0;
};

#endif
