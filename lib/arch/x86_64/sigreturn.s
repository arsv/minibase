.equ NR_rt_sigreturn, 15

.globl sigreturn

sigreturn:
	movl	$NR_rt_sigreturn, %eax
	syscall

.type sigreturn,@function
.size sigreturn,.-sigreturn
