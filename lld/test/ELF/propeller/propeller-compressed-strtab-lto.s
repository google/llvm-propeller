# REQUIRES: x86
## Test control flow graph is created.

# RUN: clang -c -flto=thin -fbasic-block-sections=all -O2 %S/Inputs/sample.c -o %t.o
# RUN: file %t.o | FileCheck %s --check-prefix=FILETYPE 
# FILETYPE: LLVM IR bitcode

# RUN: clang -fuse-ld=lld -fbasic-block-sections=all -Wl,-lto-basicblock-sections=all -Wl,-propeller=%S/Inputs/propeller.data -Wl,-propeller-keep-named-symbols -O2 %t.o -o %t.out

## We shall have "a.BB.main", "aa.BB.main", "aaa.BB.main", "aaaa.BB.main" in the symbol table.
# RUN: llvm-nm %t.out | grep -cF ".BB.main" | FileCheck %s --check-prefix=SYM_COUNT
# SYM_COUNT: 4
## But we only have "aaaa.BB.main" in .strtab, all others are compressed.
# RUN: llvm-readelf --string-dump=.strtab %t.out | grep -cF ".BB.main" | FileCheck %s --check-prefix=STRTAB_COUNT
# STRTAB_COUNT: 1

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
