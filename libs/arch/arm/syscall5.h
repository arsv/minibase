#ifndef __SYSCALL5_H__
#define __SYSCALL5_H__

inline static long syscall5(int nr, long a1, long a2, long a3, long a4, long a5)
{
	register long r0 asm("r7") = nr;
	register long r1 asm("r0") = a1;
	register long r2 asm("r1") = a2;
	register long r3 asm("r2") = a3;
	register long r4 asm("r3") = a4;
	register long r5 asm("r4") = a5;

	asm volatile ("svc 0" : "=r"(r0)
		: "r"(r0), "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5));
	
	return r0;
}

#endif
