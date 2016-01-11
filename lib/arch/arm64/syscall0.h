#ifndef __SYSCALL0_H__
#define __SYSCALL0_H__

inline static long syscall0(int nr)
{
	register long x8 asm("x8") = nr;
	register long x0 asm("x0");

	asm volatile ("svc 0" : "=r"(x0) : "r"(x8));
	
	return x0;
}

#endif
