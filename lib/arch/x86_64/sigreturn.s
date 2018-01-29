.equ NR_rt_sigreturn, 15

.globl sigreturn

sigreturn:
	movq    $NR_rt_sigreturn, %rax
	syscall

.size sigreturn,.-sigreturn
.type sigreturn,function
