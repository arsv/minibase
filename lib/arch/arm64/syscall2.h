#ifndef __SYSCALL2_H__
#define __SYSCALL2_H__

inline static long syscall2(int nr, long a1, long a2)
{
	register long x8 asm("x8") = nr;
	register long x0 asm("x0") = a1;
	register long x1 asm("x1") = a2;

	asm volatile ("svc 0" : "=r"(x0)
		: "r"(x8), "r"(x0), "r"(x1));
	
	return x0;
}

#endif
