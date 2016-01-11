#ifndef __SYSCALL_H__
#define __SYSCALL_H__

inline static long syscall0(int nr)
{
	register long r7 asm("r7") = nr;
	register long r0 asm("r0");

	asm volatile ("svc 0" : "=r"(r0) : "r"(r7));
	
	return r0;
}

inline static long syscall1(int nr, long a1)
{
	register long r7 asm("r7") = nr;
	register long r0 asm("r0") = a1;

	asm volatile ("svc 0" : "=r"(r0) : "r"(r7), "r"(r0));
	
	return r0;
}

inline static long syscall2(int nr, long a1, long a2)
{
	register long r7 asm("r7") = nr;
	register long r0 asm("r0") = a1;
	register long r1 asm("r1") = a2;

	asm volatile ("svc 0" : "=r"(r0)
		: "r"(r7), "r"(r0), "r"(r1));
	
	return r0;
}

inline static long syscall3(int nr, long a1, long a2, long a3)
{
	register long r7 asm("r7") = nr;
	register long r0 asm("r0") = a1;
	register long r1 asm("r1") = a2;
	register long r2 asm("r2") = a3;

	asm volatile ("svc 0" : "=r"(r0)
		: "r"(r7), "r"(r0), "r"(r1), "r"(r2));
	
	return r0;
}

inline static long syscall4(int nr, long a1, long a2, long a3, long a4)
{
	register long r7 asm("r7") = nr;
	register long r0 asm("r0") = a1;
	register long r1 asm("r1") = a2;
	register long r2 asm("r2") = a3;
	register long r3 asm("r3") = a4;

	asm volatile ("svc 0" : "=r"(r0)
		: "r"(r7), "r"(r0), "r"(r1), "r"(r2), "r"(r3));
	
	return r0;
}

inline static long syscall5(int nr, long a1, long a2, long a3, long a4, long a5)
{
	register long r7 asm("r7") = nr;
	register long r0 asm("r0") = a1;
	register long r1 asm("r1") = a2;
	register long r2 asm("r2") = a3;
	register long r3 asm("r3") = a4;
	register long r4 asm("r4") = a5;

	asm volatile ("svc 0" : "=r"(r0)
		: "r"(r7), "r"(r0), "r"(r1), "r"(r2), "r"(r3), "r"(r4));
	
	return r0;
}

inline static long syscall6(int nr, long a1, long a2, long a3, long a4,
		long a5, long a6)
{
	register long r7 asm("r7") = nr;
	register long r0 asm("r0") = a1;
	register long r1 asm("r1") = a2;
	register long r2 asm("r2") = a3;
	register long r3 asm("r3") = a4;
	register long r4 asm("r4") = a5;
	register long r5 asm("r5") = a6;

	asm volatile ("svc 0" : "=r"(r0)
	: "r"(r7), "r"(r0), "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5));
	
	return r0;
}

#endif
