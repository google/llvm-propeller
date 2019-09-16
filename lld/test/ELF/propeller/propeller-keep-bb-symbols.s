# RUN: llvm-mc -filetype=obj -triple=x86_64-pc-linux %s -o %t.o
# RUN: ld.lld -propeller=%S/Inputs/propeller-2.data -propeller-dump-symbol-order=$(dirname %t.o)/symbol-order-2 %t.o -o %t.out
# RUN: cat $(dirname %t.o)/symbol-order-2 | FileCheck %s --check-prefix=SYM_ORDER
# SYM_ORDER: Hot
# SYM_ORDER: aaaa.BB.main
# SYM_ORDER: aaaaaa.BB.main

# Symbol "aaaaaa.BB.main" is removed because it is adjacent to aaaa.BB.main.
# RUN: [[ -z `llvm-nm -S %t.out | grep -F "aaaaaa.BB.main"` ]]
# Symbol "aaaa.BB.main" is kept, because it starts a new cold code section for function "main".
# Run: [[ -n `llvm-nm -S %t.out | grep -F "aaaa.BB.main"` ]]


	.text
	.file	"propeller-keep-bb-symbols.c"
	.section	.rodata.cst16,"aM",@progbits,16
	.p2align	4               # -- Begin function this_is_very_code
.LCPI0_0:
	.quad	4640642756656496640     # double 183
	.zero	8
	.section	.text.this_is_very_code,"ax",@progbits
	.globl	this_is_very_code
	.p2align	4, 0x90
	.type	this_is_very_code,@function
this_is_very_code:                      # @this_is_very_code
	.cfi_startproc
# %bb.0:                                # %entry
	movabsq	$4749492747822432256, %rax # imm = 0x41E9967D81400000
	movq	%rax, -8(%rsp)
	movabsq	$4722860923688058880, %rax # imm = 0x418AF8FCC0000000
	movq	%rax, -16(%rsp)
	movhpd	-8(%rsp), %xmm0         # xmm0 = xmm0[0],mem[0]
	movapd	.LCPI0_0(%rip), %xmm1   # xmm1 = <1.83E+2,u>
	movhpd	-16(%rsp), %xmm1        # xmm1 = xmm1[0],mem[0]
	movsd	-16(%rsp), %xmm2        # xmm2 = mem[0],zero
	divsd	-8(%rsp), %xmm2
	divpd	%xmm1, %xmm0
	movapd	%xmm0, %xmm1
	unpckhpd	%xmm0, %xmm1    # xmm1 = xmm1[1],xmm0[1]
	addsd	%xmm2, %xmm1
	addsd	%xmm0, %xmm1
	movapd	%xmm1, %xmm0
	retq
	.cfi_endproc
.Lfunc_end0:
	.size	this_is_very_code, .Lfunc_end0-this_is_very_code
                                        # -- End function
	.section	.text.compute_flag,"ax",@progbits
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
	.cfi_endproc
.Lfunc_end1:
	.size	compute_flag, .Lfunc_end1-compute_flag
                                        # -- End function
	.section	.text.main,"ax",@progbits
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
	.cfi_endproc
	.section	.text.main,"ax",@progbits,unique,1
	.p2align	4, 0x90         # %for.inc
                                        #   in Loop: Header=BB2_1 Depth=1
aaaaa.BB.main:
	.cfi_startproc
	.cfi_def_cfa %rsp, 32
	.cfi_offset %rbx, -16
	addl	$1, %ebx
	cmpl	$2000000000, %ebx       # imm = 0x77359400
	je	aaaaaa.BB.main
	jmp	a.BB.main
.Ltmp0:
	.size	aaaaa.BB.main, .Ltmp0-aaaaa.BB.main
	.cfi_endproc
	.section	.text.main,"ax",@progbits,unique,2
a.BB.main:                              # %for.body
                                        # =>This Inner Loop Header: Depth=1
	.cfi_startproc
	.cfi_def_cfa %rsp, 32
	.cfi_offset %rbx, -16
	movl	%ebx, %edi
	callq	compute_flag
	addl	$1, count(%rip)
	testl	%eax, %eax
	je	aaa.BB.main
	jmp	aa.BB.main
.Ltmp1:
	.size	a.BB.main, .Ltmp1-a.BB.main
	.cfi_endproc
	.section	.text.main,"ax",@progbits,unique,3
aa.BB.main:                             # %if.then
                                        #   in Loop: Header=BB2_1 Depth=1
	.cfi_startproc
	.cfi_def_cfa %rsp, 32
	.cfi_offset %rbx, -16
	movsd	(%rsp), %xmm0           # xmm0 = mem[0],zero
	divsd	8(%rsp), %xmm0
	movsd	8(%rsp), %xmm1          # xmm1 = mem[0],zero
	divsd	(%rsp), %xmm1
	addsd	%xmm0, %xmm1
	addsd	(%rsp), %xmm1
	movsd	%xmm1, (%rsp)
	jmp	aaa.BB.main
.Ltmp2:
	.size	aa.BB.main, .Ltmp2-aa.BB.main
	.cfi_endproc
	.section	.text.main,"ax",@progbits,unique,4
aaa.BB.main:                            # %if.end
                                        #   in Loop: Header=BB2_1 Depth=1
	.cfi_startproc
	.cfi_def_cfa %rsp, 32
	.cfi_offset %rbx, -16
	movslq	count(%rip), %rax
	imulq	$2089394539, %rax, %rcx # imm = 0x7C89A16B
	movq	%rcx, %rdx
	shrq	$63, %rdx
	sarq	$58, %rcx
	addl	%edx, %ecx
	imull	$137949234, %ecx, %ecx  # imm = 0x838F032
	subl	%ecx, %eax
	cmpl	$183, %eax
	jne	aaaaa.BB.main
	jmp	aaaa.BB.main
.Ltmp3:
	.size	aaa.BB.main, .Ltmp3-aaa.BB.main
	.cfi_endproc
	.section	.text.main,"ax",@progbits,unique,5
aaaa.BB.main:                           # %if.then4
                                        #   in Loop: Header=BB2_1 Depth=1
	.cfi_startproc
	.cfi_def_cfa %rsp, 32
	.cfi_offset %rbx, -16
	xorps	%xmm0, %xmm0
	cvtsi2sdl	count(%rip), %xmm0
	callq	this_is_very_code
	addsd	(%rsp), %xmm0
	movsd	%xmm0, (%rsp)
	jmp	aaaaa.BB.main
.Ltmp4:
	.size	aaaa.BB.main, .Ltmp4-aaaa.BB.main
	.cfi_endproc
	.section	.text.main,"ax",@progbits,unique,6
aaaaaa.BB.main:                         # %for.end
	.cfi_startproc
	.cfi_def_cfa %rsp, 32
	.cfi_offset %rbx, -16
	xorl	%eax, %eax
	addq	$16, %rsp
	.cfi_def_cfa_offset 16
	popq	%rbx
	.cfi_def_cfa_offset 8
	retq
.Ltmp5:
	.size	aaaaaa.BB.main, .Ltmp5-aaaaaa.BB.main
	.cfi_endproc
	.section	.text.main,"ax",@progbits
.Lfunc_end2:
	.size	main, .Lfunc_end2-main
                                        # -- End function
	.type	count,@object           # @count
	.comm	count,4,4

	.ident	"clang version 10.0.0 (git@github.com:google/llvm-propeller.git 7a42aa9481e359801d413bf4ad9e505108b6711d)"
	.section	".note.GNU-stack","",@progbits
	.addrsig
	.addrsig_sym count
