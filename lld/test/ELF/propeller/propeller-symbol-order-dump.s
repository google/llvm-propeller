# REQUIRES: x86
## Test dump symbol order works good.

# RUN: llvm-mc -filetype=obj -triple=x86_64 %s -o %t.o
# RUN: ld.lld -propeller=%S/Inputs/propeller.data -propeller-dump-symbol-order=%t2.symorder %t.o -o %t.out

# RUN: cat %t2.symorder | FileCheck %s --check-prefix=SYMBOL_ORDER

# SYMBOL_ORDER: main
# SYMBOL_ORDER: aa.BB.main
# SYMBOL_ORDER: aaa.BB.main
# SYMBOL_ORDER: a.BB.main
# SYMBOL_ORDER: compute_flag
# SYMBOL_ORDER: Hot
# SYMBOL_ORDER: aaaa.BB.main

.text
	.section	.text.compute_flag,"ax",@progbits
	.globl	compute_flag
	.type	compute_flag,@function
compute_flag:
	movslq	%edi, %rcx
	retq
.Lfunc_end0:
	.size	compute_flag, .Lfunc_end0-compute_flag

	.section	.text.main,"ax",@progbits
	.globl	main
	.type	main,@function
main:
	pushq	%rbx
	jmp	a.BB.main    # 0 -> 1

	.section	.text.main,"ax",@progbits,unique,1
aaa.BB.main:
	je	aaaa.BB.main   # 3 -> 4
	jmp	a.BB.main      # 3 -> 1  
.Ltmp0:
	.size	aaa.BB.main, .Ltmp0-aaa.BB.main
        
	.section	.text.main,"ax",@progbits,unique,2
a.BB.main:
	je	aaa.BB.main   # 1 -> 3
	jmp	aa.BB.main    # 1 -> 2
.Ltmp1:
	.size	a.BB.main, .Ltmp1-a.BB.main

	.section	.text.main,"ax",@progbits,unique,3
aa.BB.main:
	jmp	aaa.BB.main   # 2 -> 3
.Ltmp2:
	.size	aa.BB.main, .Ltmp2-aa.BB.main

	.section	.text.main,"ax",@progbits,unique,4
aaaa.BB.main:
	retq
.Ltmp3:
	.size	aaaa.BB.main, .Ltmp3-aaaa.BB.main

	.section	.text.main,"ax",@progbits
.Lfunc_end1:
	.size	main, .Lfunc_end1-main
