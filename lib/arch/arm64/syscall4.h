#ifndef __SYSCALL4_H__
#define __SYSCALL4_H__

inline static long syscall4(int nr, long a1, long a2, long a3, long a4)
{
	register long x8 asm("x8") = nr;
	register long x0 asm("x0") = a1;
	register long x1 asm("x1") = a2;
	register long x2 asm("x2") = a3;
	register long x3 asm("x3") = a4;

	asm volatile ("svc 0" : "=r"(x0)
		: "r"(x8), "r"(x0), "r"(x1), "r"(x2), "r"(x3));
	
	return x0;
}

#endif
