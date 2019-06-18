# REQUIRES: x86
## Basic propeller tests.
## This test exercises optimal basic block reordering one a single function.

# RUN: llvm-mc -filetype=obj -triple=x86_64-pc-linux %s -o %t.o
# RUN: ld.lld  %t.o -o %t.out
# RUN: llvm-nm -Sn %t.out| FileCheck %s --check-prefix=NM1

# NM1:	0000000000201000 0000000000000005 t foo
# NM1:	0000000000201005 0000000000000005 t a.BB.foo
# NM1:	000000000020100a 0000000000000008 t aa.BB.foo
# NM1:	0000000000201012 0000000000000008 t aaa.BB.foo

# RUN: llvm-objdump -s %t.out| FileCheck %s --check-prefix=BEFORE

# BEFORE:      Contents of section .text:
# BEFORE-NEXT:  201000 0f1f0074 0d0f1f00 74082200 00000000
# BEFORE-NEXT:  201010 00003300 00000000 0000

## Create a propeller profile for foo, based on the cfg below:
##
##
##     foo
##      |\
##      | \100
##      |  \
##      |   v
##   150|  a.BB.foo
##      |   /  \
##      |  /100 \0
##      | /      \
##      vv        v
##   aaa.BB.foo  aa.BB.foo
##
## A naive fallthrough maximization approach would attach foo -> aaa.BB.foo and
## as a fallthrough edge. However, the optimal ordering is foo -> a.BB.foo -> aaa.BB.foo.
## This example is motivated by Figure 6 in https://arxiv.org/pdf/1809.04676.pdf.

# RUN: echo "Symbols" > %t_prof.propeller
# RUN: echo "1 1a Nfoo" >> %t_prof.propeller
# RUN: echo "2 5 1.1" >> %t_prof.propeller
# RUN: echo "3 8 1.2" >> %t_prof.propeller
# RUN: echo "4 8 1.3" >> %t_prof.propeller
# RUN: echo "Branches" >> %t_prof.propeller
# RUN: echo "1 4 150" >> %t_prof.propeller
# RUN: echo "2 4 100" >> %t_prof.propeller
# RUN: echo "Fallthroughs" >> %t_prof.propeller
# RUN: echo "1 2 100" >> %t_prof.propeller

# RUN: ld.lld  %t.o -propeller=%t_prof.propeller -propeller-keep-named-symbols -o %t.propeller.out
# RUN: llvm-nm -n %t.propeller.out| FileCheck %s --check-prefix=NM2

# NM2:	0000000000201000 t foo
# NM2:	0000000000201005 t a.BB.foo
# NM2:	000000000020100a t aaa.BB.foo
# NM2:	0000000000201012 t aa.BB.foo

# RUN: llvm-objdump -s %t.propeller.out| FileCheck %s --check-prefix=AFTER

# AFTER:      Contents of section .text
# AFTER-NEXT:  201000 0f1f0074 050f1f00 75083300 00000000
# AFTER-NEXT:  201010 00002200 00000000 0000
#

.section	.text,"ax",@progbits
# -- Begin function foo
.type	foo,@function
foo:
 nopl (%rax)
 je	aaa.BB.foo
 jmp	a.BB.foo

.section	.text,"ax",@progbits,unique,1
a.BB.foo:
 nopl (%rax)
 je	aaa.BB.foo
 jmp	aa.BB.foo
.La.BB.foo_end:
 .size	a.BB.foo, .La.BB.foo_end-a.BB.foo

.section	.text,"ax",@progbits,unique,2
aa.BB.foo:
 .quad	0x22
.Laa.BB.foo_end:
 .size	aa.BB.foo, .Laa.BB.foo_end-aa.BB.foo

.section	.text,"ax",@progbits,unique,3
aaa.BB.foo:
 .quad	0x33
.Laaa.BB.foo_end:
 .size	aaa.BB.foo, .Laaa.BB.foo_end-aaa.BB.foo

.section	.text,"ax",@progbits
.Lfoo_end:
 .size	foo, .Lfoo_end-foo
# -- End function foo
