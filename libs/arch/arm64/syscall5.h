#ifndef __SYSCALL5_H__
#define __SYSCALL5_H__

inline static long syscall5(int nr, long a1, long a2, long a3, long a4, long a5)
{
	register long x8 asm("x8") = nr;
	register long x0 asm("x0") = x0;
	register long x1 asm("x1") = x1;
	register long x2 asm("x2") = x2;
	register long x3 asm("x3") = x3;
	register long x4 asm("x4") = x4;

	asm volatile ("svc 0" : "=r"(x0)
		: "r"(x8), "r"(x0), "r"(x1), "r"(x2), "r"(x3), "r"(x4));
	
	return x0;
}

#endif
