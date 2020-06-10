# REQUIRES: x86
## Test propeller fails if it tries to process an object that is built with "-fbasic-block-sections=labels".

# RUN: llvm-mc -filetype=obj -triple=x86_64 %s -o %t.o
# RUN: ld.lld -propeller=%S/Inputs/propeller.data %t.o -o %t.out 2>&1 | FileCheck %s --check-prefix=CHECK

# CHECK: warning: basicblock sections must not have same section index

	.text
	.globl	compute_flag
	.p2align	4, 0x90
	.type	compute_flag,@function
compute_flag:
# %bb.0:
	movslq	%edi, %rcx
	retq
.Ltmp0:
	.size	.BB.compute_flag, .Ltmp0-.BB.compute_flag
.Lfunc_end0:
	.size	compute_flag, .Lfunc_end0-compute_flag
	.globl	main
	.p2align	4, 0x90
	.type	main,@function
main:
# %bb.0:
	pushq	%rbx
	subq	$16, %rsp
	jmp	a.BB.main
.Ltmp1:
	.size	.BB.main, .Ltmp1-.BB.main
	.p2align	4, 0x90
aaa.BB.main:
	addl	$1, %ebx
	cmpl	$2000000000, %ebx
	je	aaaa.BB.main
.Ltmp2:
	.size	aaa.BB.main, .Ltmp2-aaa.BB.main
a.BB.main:
	movl	%ebx, %edi
	callq	compute_flag
	addl	$1, count(%rip)
	testl	%eax, %eax
	je	aaa.BB.main
.Ltmp3:
	.size	a.BB.main, .Ltmp3-a.BB.main
aa.BB.main:
	movsd	(%rsp), %xmm0
	jmp	aaa.BB.main
.Ltmp4:
	.size	aa.BB.main, .Ltmp4-aa.BB.main
aaaa.BB.main:
	xorl	%eax, %eax
	popq	%rbx
	retq
.Ltmp5:
	.size	aaaa.BB.main, .Ltmp5-aaaa.BB.main
.Lfunc_end1:
	.size	main, .Lfunc_end1-main
	.type	count,@object
	.comm	count,4,4
