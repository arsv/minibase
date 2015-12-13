.text
.global _start
.global _exit

_start:
	popq	%rdi			/* %rdi = argc */
	movq	%rsp,%rsi		/* %rsi = argv */
	leaq	8(%rsi,%rdi,8),%rdx	/* %rdx = envp = (8*rdi)+%rsi+8 */
	
	call	main

	movq	%rax, %rdi

_exit:	mov	$0x3C, %ax		/* call _exit */
	movzwl	%ax, %eax
	mov	%rcx, %r10
	syscall
	hlt				/* catch fire and die */

.type _exit,@function
.size _exit,.-_exit

.type _start,@function
.size _start,_exit-_start
