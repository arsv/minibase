.equ NR_rt_sigreturn, 173

.globl sigreturn

sigreturn:
	mov     r7, $NR_rt_sigreturn
	svc     0

.size sigreturn,.-sigreturn
.type sigreturn,function

.section .note.GNU-stack,"",%progbits
