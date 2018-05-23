.equ NR_exit, 1

.text
.global _start
.global _exit

_start:
	popl    %ecx            /* %ecx = argc */
	movl    %esp, %eax      /* %eax = argv */
	pushl   %ecx
	pushl   %eax
	pushl   %ecx

	call    main

	movl    %eax, %ebx      /* return value */
1:	xor     %eax, %eax
	movb    $NR_exit, %al   /* call _exit */
	int     $0x80
	hlt
_exit:
	movl    4(%esp), %ebx
	jmp     1b

.type _exit,@function
.size _exit,.-_exit

.type _start,@function
.size _start,_exit-_start
