# RUN: llvm-mc -filetype=obj -triple=x86_64 %s -o %t.o
# RUN: ld.lld -propeller=%S/Inputs/propeller-2.data -propeller-dump-symbol-order=%t2.symorder %t.o -o %t.out
# RUN: cat %t2.symorder | FileCheck %s --check-prefix=SYM_ORDER
# SYM_ORDER: Hot
# SYM_ORDER: aaaa.BB.main
# SYM_ORDER: aaaaaa.BB.main

## Symbol "aaaaaa.BB.main" is removed because it is adjacent to aaaa.BB.main.
# RUN: { llvm-nm %t.out | grep -cF "aaaaaa.BB.main" 2>&1 || true ; } | FileCheck %s --check-prefix=SYM_COUNT
# SYM_COUNT: 0

## Symbol "aaaa.BB.main" is kept, because it starts a new cold code section for function "main".
# Run: llvm-nm -S %t.out | grep -cF "aaaa.BB.main" | FileCheck %s --check-prefix=SYM_COUNT1
# SYM_COUNT1: 1

	.text
	.section	.rodata.cst16,"aM",@progbits,16
	.p2align	4
.LCPI0_0:
	.quad	4640642756656496640
	.zero	8
	.section	.text.this_is_very_cold,"ax",@progbits
	.globl	this_is_very_cold
	.p2align	4, 0x90
	.type	this_is_very_cold,@function
this_is_very_cold:
# %bb.0:
	movabsq	$4749492747822432256, %rax
	retq
.Lfunc_end0:
	.size	this_is_very_cold, .Lfunc_end0-this_is_very_cold
	.section	.text.compute_flag,"ax",@progbits

	.globl	compute_flag
	.p2align	4, 0x90
	.type	compute_flag,@function
compute_flag:
# %bb.0:
	movslq	%edi, %rcx
	cmpl	$4, %edx
	cmovll	%ecx, %eax
	retq
.Lfunc_end1:
	.size	compute_flag, .Lfunc_end1-compute_flag

	.section	.text.main,"ax",@progbits
	.globl	main
	.p2align	4, 0x90
	.type	main,@function
main:
# %bb.0:
	pushq	%rbx
	subq	$16, %rsp
	movabsq	$4742870812841738240, %rax # imm = 0x41D20FE01F000000
	jmp	a.BB.main
	.section	.text.main,"ax",@progbits,unique,1
	.p2align	4, 0x90

aaaaa.BB.main:
	addl	$1, %ebx
	cmpl	$2000000000, %ebx
	je	aaaaaa.BB.main
	jmp	a.BB.main
.Ltmp0:
	.size	aaaaa.BB.main, .Ltmp0-aaaaa.BB.main
	.section	.text.main,"ax",@progbits,unique,2
a.BB.main:

	movl	%ebx, %edi
	callq	compute_flag
	addl	$1, count(%rip)
	testl	%eax, %eax
	je	aaa.BB.main
	jmp	aa.BB.main
.Ltmp1:
	.size	a.BB.main, .Ltmp1-a.BB.main
	.section	.text.main,"ax",@progbits,unique,3
aa.BB.main:

	movsd	(%rsp), %xmm0
	jmp	aaa.BB.main
.Ltmp2:
	.size	aa.BB.main, .Ltmp2-aa.BB.main
	.section	.text.main,"ax",@progbits,unique,4
aaa.BB.main:

	movslq	count(%rip), %rax
	cmpl	$183, %eax
	jne	aaaaa.BB.main
	jmp	aaaa.BB.main
.Ltmp3:
	.size	aaa.BB.main, .Ltmp3-aaa.BB.main
	.section	.text.main,"ax",@progbits,unique,5
aaaa.BB.main:

	xorps	%xmm0, %xmm0
	cvtsi2sdl	count(%rip), %xmm0
	callq	this_is_very_cold
	addsd	(%rsp), %xmm0
	movsd	%xmm0, (%rsp)
	jmp	aaaaa.BB.main
.Ltmp4:
	.size	aaaa.BB.main, .Ltmp4-aaaa.BB.main
	.section	.text.main,"ax",@progbits,unique,6
aaaaaa.BB.main:
	xorl	%eax, %eax
	addq	$16, %rsp
	popq	%rbx
	retq
.Ltmp5:
	.size	aaaaaa.BB.main, .Ltmp5-aaaaaa.BB.main
	.section	.text.main,"ax",@progbits
.Lfunc_end2:
	.size	main, .Lfunc_end2-main

	.type	count,@object
	.comm	count,4,4
