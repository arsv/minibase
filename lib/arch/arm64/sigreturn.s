.equ NR_rt_sigreturn, 139

.globl sigreturn

sigreturn:
	mov    x8, #NR_rt_sigreturn
	svc    0

.type sigreturn,function
.size sigreturn,.-sigreturn
