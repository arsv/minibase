.globl syscall
.text

syscall:
	push    %edi
	push    %esi
	push    %ebx
	push    %ebp

	movl    %esp, %edi
	movl    5*4(%edi), %eax
	movl    6*4(%edi), %ebx
	movl    7*4(%edi), %ecx
	movl    8*4(%edi), %edx
	movl    9*4(%edi), %esi
	movl   11*4(%edi), %ebp
	movl   10*4(%edi), %edi

	int     $0x80

	pop     %ebp
	pop     %ebx
	pop     %esi
	pop     %edi

	ret

.type syscall,function
.size syscall,.-syscall

.section .note.GNU-stack,"",%progbits
