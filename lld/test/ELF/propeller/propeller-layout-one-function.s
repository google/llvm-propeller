# REQUIRES: x86
## Basic propeller tests.
## This test exercises basic block reordering on a single function.

# RUN: llvm-mc -filetype=obj -triple=x86_64-pc-linux %s -o %t.o
# RUN: ld.lld  %t.o -o %t.out
# RUN: llvm-nm -n %t.out| FileCheck %s --check-prefix=NM1

# NM1:	0000000000201000 t foo
# NM1:	0000000000201003 t foo.bb.1
# NM1:	0000000000201008 t foo.bb.2
# NM1:	000000000020100b t foo.bb.3
# NM1:	0000000000201010 t foo.bb.4

# RUN: llvm-objdump -s %t.out| FileCheck %s --check-prefix=BEFORE

# BEFORE:      Contents of section .foo:
# BEFORE-NEXT:  201000 0f1f000f 1f007403 0f1f000f 1f0075f3
# BEFORE-NEXT:  201010 44000000 00000000

## Create a propeller profile for foo, based on the cfg below:
##
##             foo
##              |
##              |5
##              V
##      +--> foo.bb.1 <-+
##      |      / \      |
##     0|    0/   \95   |90
##      |    /     \    |
##      |   v       \   |
##  foo.bb.2         v  |
##      \        foo.bb.3
##       \           /
##        \         /
##         \       /10
##          \     /
##           v   v
##          foo.bb.4
##

# RUN: echo "Symbols" > %t_prof.propeller
# RUN: echo "1 18 Nfoo" > %t_prof.propeller
# RUN: echo "2 5 1.1" >> %t_prof.propeller
# RUN: echo "3 3 1.2" >> %t_prof.propeller
# RUN: echo "4 5 1.3" >> %t_prof.propeller
# RUN: echo "5 b 1.4" >> %t_prof.propeller
# RUN: echo "Branches" >> %t_prof.propeller
# RUN: echo "4 2 90" >> %t_prof.propeller
# RUN: echo "2 4 95" >> %t_prof.propeller
# RUN: echo "Fallthroughs" >> %t_prof.propeller
# RUN: echo "4 5 10" >> %t_prof.propeller
# RUN: echo "1 2 5" >> %t_prof.propeller

# RUN: ld.lld  %t.o -propeller=%t_prof.propeller -split-functions -reorder-functions -reorder-blocks -o %t.propeller.out
# RUN: llvm-nm -n %t.propeller.out| FileCheck %s --check-prefix=NM2

# NM2:	0000000000201000 t foo
# NM2:	0000000000201003 t foo.bb.1
# NM2:	0000000000201008 t foo.bb.3
# NM2:	000000000020100d t foo.bb.4
# NM2:	0000000000201015 t foo.bb.2

# RUN: llvm-objdump -s %t.propeller.out| FileCheck %s --check-prefix=AFTER

# AFTER:      Contents of section .foo:
# AFTER-NEXT:  201000 0f1f000f 1f00750d 0f1f0075 f6440000
# AFTER-NEXT:  201010 00000000 000f1f00



.section	.foo,"ax",@progbits
# -- Begin function foo
.type	foo,@function
foo:
 nopl (%rax)
 jmp	foo.bb.1

.section	.foo,"ax",@progbits,unique,1
foo.bb.1:
 nopl (%rax)
 je	foo.bb.3
 jmp	foo.bb.2
.Lfoo_bb_1_end:
 .size	foo.bb.1, .Lfoo_bb_1_end-foo.bb.1

.section	.foo,"ax",@progbits,unique,2
foo.bb.2:
 nopl (%rax)
 jmp	foo.bb.3
.Lfoo_bb_2_end:
 .size	foo.bb.2, .Lfoo_bb_2_end-foo.bb.2

.section	.foo,"ax",@progbits,unique,3
foo.bb.3:
 nopl (%rax)
 jne	foo.bb.1
 jmp	foo.bb.4
.Lfoo_bb_3_end:
 .size	foo.bb.3, .Lfoo_bb_3_end-foo.bb.3

.section	.foo,"ax",@progbits,unique,4
foo.bb.4:
 .quad	0x44
.Lfoo_bb_4_end:
 .size	foo.bb.4, .Lfoo_bb_4_end-foo.bb.4

.section	.foo,"ax",@progbits
.Lfoo_end:
 .size	foo, .Lfoo_end-foo
# -- End function (foo)
