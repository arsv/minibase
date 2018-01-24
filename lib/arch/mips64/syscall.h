#ifndef __SYSCALL_H__
#define __SYSCALL_H__

#include "bits/syscall.h"
#include "bits/errno.h"

#define CLOBBERS \
	"$1", "$3", "$10", "$11", "$12", "$13", \
	"$14", "$15", "$24", "$25", "hi", "lo", "memory"

static inline long syscall0(long n)
{
	register long v0 asm("v0") = n;
	register long a3 asm("a3");

	asm volatile (
		"syscall"

	: "=r"(v0), "=r"(a3)
	: "0"(v0), "1"(a3)
	: CLOBBERS);

	return a3 ? -v0 : v0;
}

static inline long syscall1(long n, long a)
{
	register long v0 asm("v0") = n;
	register long a0 asm("a0") = a;
	register long a3 asm("a3");

	asm volatile (
		"syscall"

	: "=r"(v0), "=r"(a3)
	: "0"(v0), "1"(a3), "r"(a0)
	: CLOBBERS);

	return a3 ? -v0 : v0;
}

static inline long syscall2(long n, long a, long b)
{
	register long v0 asm("v0") = n;
	register long a0 asm("a0") = a;
	register long a1 asm("a1") = b;
	register long a3 asm("a3");

	asm volatile (
		"syscall"

	: "=r"(v0), "=r"(a3)
	: "0"(v0), "1"(a3), "r"(a1), "r"(a0)
	: CLOBBERS);

	return a3 ? -v0 : v0;
}

static inline long syscall3(long n, long a, long b, long c)
{
	register long v0 asm("v0") = n;
	register long a0 asm("a0") = a;
	register long a1 asm("a1") = b;
	register long a2 asm("a2") = c;
	register long a3 asm("a3");

	asm volatile (
		"syscall"

	: "=r"(v0), "=r"(a3)
	: "0"(v0), "1"(a3), "r"(a2), "r"(a1), "r"(a0)
	: CLOBBERS);

	return a3 ? -v0 : v0;
}

static inline long syscall4(long n, long a, long b, long c, long d)
{
	register long v0 asm("v0") = n;
	register long a0 asm("a0") = a;
	register long a1 asm("a1") = b;
	register long a2 asm("a2") = c;
	register long a3 asm("a3") = d;

	asm volatile (
		"syscall"

	: "=r"(v0), "=r"(a3)
	: "0"(v0), "1"(a3), "r"(a2), "r"(a1), "r"(a0)
	: CLOBBERS);

	return a3 ? -v0 : v0;
}

static inline long syscall5(long n, long a, long b, long c, long d, long e)
{
	register long v0 asm("v0") = n;
	register long a0 asm("a0") = a;
	register long a1 asm("a1") = b;
	register long a2 asm("a2") = c;
	register long a3 asm("a3") = d;
	register long a4 asm("t0") = e;

	asm volatile (
		"syscall"

	: "=r"(v0), "=r"(a3)
	: "0"(v0), "1"(a3), "r"(a2), "r"(a1), "r"(a0), "r"(a4)
	: CLOBBERS);

	return a3 ? -v0 : v0;
}

static inline long syscall6(long n, long a, long b, long c, long d, long e, long f)
{
	register long v0 asm("v0") = n;
	register long a0 asm("a0") = a;
	register long a1 asm("a1") = b;
	register long a2 asm("a2") = c;
	register long a3 asm("a3") = d;
	register long a4 asm("t0") = e;
	register long a5 asm("t1") = f;

	asm volatile (
		"syscall"

	: "=r"(v0), "=r"(a3)
	: "0"(v0), "1"(a3), "r"(a2), "r"(a1), "r"(a0), "r"(a4), "r"(a5)
	: CLOBBERS);

	return a3 ? -v0 : v0;
}

#endif
