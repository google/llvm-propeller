# REQUIRES: x86
## Test propeller fails if it tries to process an object that is built with "-fbasicblock-sections=labels".

# RUN: llvm-mc -filetype=obj -triple=x86_64-pc-linux %s -o %t.o
# RUN: not ld.lld -propeller=%S/Inputs/propeller.data %t.o -o %t.out 1>%t2.out 2>&1
# RUN: cat %t2.out | FileCheck %s --check-prefix=CHECK

# CHECK: Basicblock sections must not have same section index

	.text
	.file	"sample.c"
	.globl	compute_flag            # -- Begin function compute_flag
	.p2align	4, 0x90
	.type	compute_flag,@function
compute_flag:                           # @compute_flag
	.cfi_startproc
# %bb.0:                                # %entry
	movslq	%edi, %rcx
	imulq	$1717986919, %rcx, %rax # imm = 0x66666667
	movq	%rax, %rdx
	shrq	$63, %rdx
	sarq	$34, %rax
	addl	%edx, %eax
	addl	%eax, %eax
	leal	(%rax,%rax,4), %eax
	movl	%ecx, %edx
	subl	%eax, %edx
	addl	$1, %ecx
	xorl	%eax, %eax
	cmpl	$4, %edx
	cmovll	%ecx, %eax
	retq
.Ltmp0:
	.size	.BB.compute_flag, .Ltmp0-.BB.compute_flag
.Lfunc_end0:
	.size	compute_flag, .Lfunc_end0-compute_flag
	.cfi_endproc
                                        # -- End function
	.globl	main                    # -- Begin function main
	.p2align	4, 0x90
	.type	main,@function
main:                                   # @main
	.cfi_startproc
# %bb.0:                                # %entry
	pushq	%rbx
	.cfi_def_cfa_offset 16
	subq	$16, %rsp
	.cfi_def_cfa_offset 32
	.cfi_offset %rbx, -16
	movabsq	$4742870812841738240, %rax # imm = 0x41D20FE01F000000
	movq	%rax, (%rsp)
	movabsq	$4683066038424698880, %rax # imm = 0x40FD97C000000000
	movq	%rax, 8(%rsp)
	xorl	%ebx, %ebx
	jmp	a.BB.main
.Ltmp1:
	.size	.BB.main, .Ltmp1-.BB.main
	.p2align	4, 0x90
aaa.BB.main:                            # %for.inc
                                        #   in Loop: Header=BB1_1 Depth=1
	addl	$1, %ebx
	cmpl	$2000000000, %ebx       # imm = 0x77359400
	je	aaaa.BB.main
.Ltmp2:
	.size	aaa.BB.main, .Ltmp2-aaa.BB.main
a.BB.main:                              # %for.body
                                        # =>This Inner Loop Header: Depth=1
	movl	%ebx, %edi
	callq	compute_flag
	addl	$1, count(%rip)
	testl	%eax, %eax
	je	aaa.BB.main
.Ltmp3:
	.size	a.BB.main, .Ltmp3-a.BB.main
aa.BB.main:                             # %if.then
                                        #   in Loop: Header=BB1_1 Depth=1
	movsd	(%rsp), %xmm0           # xmm0 = mem[0],zero
	divsd	8(%rsp), %xmm0
	movsd	8(%rsp), %xmm1          # xmm1 = mem[0],zero
	divsd	(%rsp), %xmm1
	addsd	%xmm0, %xmm1
	addsd	(%rsp), %xmm1
	movsd	%xmm1, (%rsp)
	jmp	aaa.BB.main
.Ltmp4:
	.size	aa.BB.main, .Ltmp4-aa.BB.main
aaaa.BB.main:                           # %for.end
	xorl	%eax, %eax
	addq	$16, %rsp
	.cfi_def_cfa_offset 16
	popq	%rbx
	.cfi_def_cfa_offset 8
	retq
.Ltmp5:
	.size	aaaa.BB.main, .Ltmp5-aaaa.BB.main
.Lfunc_end1:
	.size	main, .Lfunc_end1-main
	.cfi_endproc
                                        # -- End function
	.type	count,@object           # @count
	.comm	count,4,4

	.ident	"clang version 10.0.0 (git@github.com:google/llvm-propeller.git ff4b848f375a6a915f6fa0350f425c08a0212b19)"
	.section	".note.GNU-stack","",@progbits
	.addrsig
	.addrsig_sym count
