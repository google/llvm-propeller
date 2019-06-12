# REQUIRES: x86
## Basic propeller tests.
## This test exercises basic block reordering on a single function.

# RUN: llvm-mc -filetype=obj -triple=x86_64-pc-linux %s -o %t.o
# RUN: ld.lld  %t.o -o %t.out
# RUN: llvm-nm -n %t.out| FileCheck %s --check-prefix=NM1

# NM1:	0000000000201000 t foo
# NM1:	0000000000201003 t a.BB.foo
# NM1:	0000000000201008 t aa.BB.foo
# NM1:	000000000020100b t aaa.BB.foo
# NM1:	0000000000201010 t aaaa.BB.foo

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
##      +--> a.BB.foo <--+
##      |      / \       |
##     0|    0/   \95    |90
##      |    /     \     |
##      |   v       \    |
##  aa.BB.foo        v   |
##      \        aaa.BB.foo
##       \           /
##        \         /
##        0\       /10
##          \     /
##           v   v
##         aaaa.BB.foo
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

# RUN: ld.lld  %t.o -propeller=%t_prof.propeller -o %t.propeller.out
# RUN: llvm-nm -n %t.propeller.out| FileCheck %s --check-prefix=NM2

# NM2:	0000000000201000 t foo
# NM2:	0000000000201003 t a.BB.foo
# NM2:	0000000000201008 t aaa.BB.foo
# NM2:	000000000020100d t aaaa.BB.foo
# NM2:	0000000000201015 t aa.BB.foo

# RUN: llvm-objdump -s %t.propeller.out| FileCheck %s --check-prefix=AFTER

# AFTER:      Contents of section .foo:
# AFTER-NEXT:  201000 0f1f000f 1f00750d 0f1f0075 f6440000
# AFTER-NEXT:  201010 00000000 000f1f00



.section	.foo,"ax",@progbits
# -- Begin function foo
.type	foo,@function
foo:
 nopl (%rax)
 jmp	a.BB.foo

.section	.foo,"ax",@progbits,unique,1
a.BB.foo:
 nopl (%rax)
 je	aaa.BB.foo
 jmp	aa.BB.foo
.La.BB.foo_end:
 .size	a.BB.foo, .La.BB.foo_end-a.BB.foo

.section	.foo,"ax",@progbits,unique,2
aa.BB.foo:
 nopl (%rax)
 jmp	aaa.BB.foo
.Laa.BB.foo_end:
 .size	aa.BB.foo, .Laa.BB.foo_end-aa.BB.foo

.section	.foo,"ax",@progbits,unique,3
aaa.BB.foo:
 nopl (%rax)
 jne	a.BB.foo
 jmp	aaaa.BB.foo
.Laaa.BB.foo_end:
 .size	aaa.BB.foo, .Laaa.BB.foo_end-aaa.BB.foo

.section	.foo,"ax",@progbits,unique,4
aaaa.BB.foo:
 .quad	0x44
.Laaaa.BB.foo_end:
 .size	aaaa.BB.foo, .Laaaa.BB.foo_end-aaaa.BB.foo

.section	.foo,"ax",@progbits
.Lfoo_end:
 .size	foo, .Lfoo_end-foo
# -- End function (foo)
