.equ NR_rt_sigreturn, 173

.globl sigreturn

sigreturn:
	mov     $NR_rt_sigreturn, %eax
	int     $0x80

.size sigreturn,.-sigreturn
.type sigreturn,function
