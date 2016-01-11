#ifndef __SYSCALL1_H__
#define __SYSCALL1_H__

inline static long syscall1(int nr, long a1)
{
	register long x8 asm("x8") = nr;
	register long x0 asm("x0") = a1;

	asm volatile ("svc 0" : "=r"(x0) : "r"(x8), "r"(x0));
	
	return x0;
}

#endif
