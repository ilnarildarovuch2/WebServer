.globl ciphersuites
.globl get_header
.globl net_recv
.globl net_send
.globl worker

.p2align 4
.type	get_header, @function
get_header:
.LFBget_header:
	.cfi_startproc
	endbr64
	pushq	%r12
	.cfi_def_cfa_offset 16
	.cfi_offset 12, -16
	pushq	%rbp
	.cfi_def_cfa_offset 24
	.cfi_offset 6, -24
	movq	%rsi, %rbp
	leaq	crlf(%rip), %rsi
	pushq	%rbx
	.cfi_def_cfa_offset 32
	.cfi_offset 3, -32
	call	strstr@PLT
	testq	%rax, %rax
	je	.Lnot_found
	cmpb	$0, 2(%rax)
	je	.Lreturn_null
	movq	%rbp, %rdi
	leaq	2(%rax), %rbx
	call	strlen@PLT
	movq	%rax, %r12
	jmp	.Lcheck_header

	.p2align 4
.Lnext_header:
	leaq	2(%rax), %rbx
	cmpb	$0, 2(%rax)
	je	.Lreturn_null

.Lcheck_header:
	movq	%r12, %rdx
	movq	%rbp, %rsi
	movq	%rbx, %rdi
	call	strncasecmp@PLT
	testl	%eax, %eax
	jne	.Lcontinue_search
	cmpb	$58, (%rbx,%r12)
	je	.Lfound_colon

.Lcontinue_search:
	leaq	crlf(%rip), %rsi
	movq	%rbx, %rdi
	call	strstr@PLT
	testq	%rax, %rax
	jne	.Lnext_header

.Lnot_found:
	popq	%rbx
	.cfi_remember_state
	.cfi_def_cfa_offset 24
	popq	%rbp
	.cfi_def_cfa_offset 16
	popq	%r12
	.cfi_def_cfa_offset 8
	ret

	.p2align 4
.Lreturn_null:
	.cfi_restore_state
	popq	%rbx
	.cfi_remember_state
	.cfi_def_cfa_offset 24
	xorl	%eax, %eax
	popq	%rbp
	.cfi_def_cfa_offset 16
	popq	%r12
	.cfi_def_cfa_offset 8
	ret

	.p2align 4
.Lfound_colon:
	.cfi_restore_state
	leaq	1(%rbx,%r12), %rbx
	cmpb	$32, (%rbx)
	jne	.Lafter_skip

	.p2align 4
.Lskip_spaces:
	addq	$1, %rbx
	cmpb	$32, (%rbx)
	je	.Lskip_spaces

.Lafter_skip:
	leaq	crlf(%rip), %rsi
	movq	%rbx, %rdi
	call	strstr@PLT
	movq	%rax, %rbp
	testq	%rax, %rax
	je	.Lno_crlf

.Lcopy_value:
	subq	%rbx, %rbp
	movl	$255, %eax
	movq	%rbx, %rsi
	cmpq	%rax, %rbp
	leaq	value.2(%rip), %rdi
	cmova	%rax, %rbp
	movq	%rbp, %rdx
	call	strncpy@PLT
	movb	$0, (%rax,%rbp)
	popq	%rbx
	.cfi_remember_state
	.cfi_def_cfa_offset 24
	popq	%rbp
	.cfi_def_cfa_offset 16
	popq	%r12
	.cfi_def_cfa_offset 8
	ret

.Lno_crlf:
	.cfi_restore_state
	movq	%rbx, %rdi
	call	strlen@PLT
	leaq	(%rbx,%rax), %rbp
	jmp	.Lcopy_value
	.cfi_endproc
.LFEget_header:
	.size	get_header, .-get_header


.p2align 4
.type	net_recv, @function
net_recv:
.LFBnet_recv:
	.cfi_startproc
	endbr64
	subq	$8, %rsp
	.cfi_def_cfa_offset 16
	movl	(%rdi), %edi
	xorl	%ecx, %ecx
	call	recv@PLT
	movl	%eax, %edx
	testl	%eax, %eax
	js	.Lrecv_error

.Lrecv_ok:
	movl	%edx, %eax
	addq	$8, %rsp
	.cfi_remember_state
	.cfi_def_cfa_offset 8
	ret

	.p2align 4
.Lrecv_error:
	.cfi_restore_state
	call	__errno_location@PLT
	movl	(%rax), %eax
	cmpl	$11, %eax
	je	.Lrecv_again
	movl	$-1, %edx
	cmpl	$4, %eax
	jne	.Lrecv_ok

.Lrecv_again:
	movl	$-26880, %edx
	addq	$8, %rsp
	.cfi_def_cfa_offset 8
	movl	%edx, %eax
	ret
	.cfi_endproc
.LFEnet_recv:
	.size	net_recv, .-net_recv


.p2align 4
.type	net_send, @function
net_send:
.LFBnet_send:
	.cfi_startproc
	endbr64
	subq	$8, %rsp
	.cfi_def_cfa_offset 16
	movl	(%rdi), %edi
	movl	$16384, %ecx
	call	send@PLT
	movl	%eax, %edx
	testl	%eax, %eax
	js	.Lsend_error

.Lsend_ok:
	movl	%edx, %eax
	addq	$8, %rsp
	.cfi_remember_state
	.cfi_def_cfa_offset 8
	ret

	.p2align 4
.Lsend_error:
	.cfi_restore_state
	call	__errno_location@PLT
	movl	(%rax), %eax
	cmpl	$4, %eax
	je	.Lsend_again
	movl	$-1, %edx
	cmpl	$11, %eax
	jne	.Lsend_ok

.Lsend_again:
	movl	$-26752, %edx
	addq	$8, %rsp
	.cfi_def_cfa_offset 8
	movl	%edx, %eax
	ret
	.cfi_endproc
.LFEnet_send:
	.size	net_send, .-net_send


.p2align 4
.type	worker, @function
worker:
.LFBworker:
	.cfi_startproc
	endbr64
	pushq	%rbx
	.cfi_def_cfa_offset 16
	.cfi_offset 3, -16
	subq	$32, %rsp
	.cfi_def_cfa_offset 48
	movq	%fs:40, %rax
	movq	%rax, 24(%rsp)
	xorl	%eax, %eax

	.p2align 4
.Lworker_loop:
	movq	%rsp, %rdi
	call	queue_pop@PLT
	movq	4(%rsp), %rsi
	movq	12(%rsp), %rdx
	movl	(%rsp), %edi
	call	handle_connection@PLT
	jmp	.Lworker_loop
	.cfi_endproc
.LFEworker:
	.size	worker, .-worker


ciphersuites:
	.long	49200	# MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384
	.long	49199	# MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256
	.long	52392	# MBEDTLS_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256
	.long	157	# MBEDTLS_TLS_RSA_WITH_AES_256_GCM_SHA384
	.long	156	# MBEDTLS_TLS_RSA_WITH_AES_128_GCM_SHA256
	.long	0

	.local	value.2
	.comm	value.2,256,32

	.section	.rodata.str1.1
crlf:
	.string	"\r\n"
