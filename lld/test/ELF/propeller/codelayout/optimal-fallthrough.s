# REQUIRES: x86
## Basic propeller tests.
## This test exercises optimal basic block reordering one a single function.

# RUN: llvm-mc -filetype=obj -triple=x86_64-pc-linux %s -o %t.o
# RUN: ld.lld  %t.o -optimize-bb-jumps -o %t.out
# RUN: llvm-nm -Sn %t.out| FileCheck %s --check-prefix=BEFORE

# BEFORE:	0000000000201120 0000000000000005 t foo
# BEFORE-NEXT:	0000000000201125 0000000000000005 t a.BB.foo
# BEFORE-NEXT:	000000000020112a 0000000000000004 t aa.BB.foo
# BEFORE-NEXT:	000000000020112e 0000000000000004 t aaa.BB.foo

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

# RUN: echo "!foo" > %t_prof.propeller
# RUN: echo "!!1" >> %t_prof.propeller
# RUN: echo "!!3" >> %t_prof.propeller
# RUN: echo "Symbols" >> %t_prof.propeller
# RUN: echo "1 12 Nfoo" >> %t_prof.propeller
# RUN: echo "2 5 1.1" >> %t_prof.propeller
# RUN: echo "3 4 1.2" >> %t_prof.propeller
# RUN: echo "4 4 1.3" >> %t_prof.propeller
# RUN: echo "Branches" >> %t_prof.propeller
# RUN: echo "1 4 150" >> %t_prof.propeller
# RUN: echo "2 4 100" >> %t_prof.propeller
# RUN: echo "Fallthroughs" >> %t_prof.propeller
# RUN: echo "1 2 100" >> %t_prof.propeller

# RUN: ld.lld %t.o -propeller-debug-symbol=foo -optimize-bb-jumps -propeller=%t_prof.propeller -propeller-keep-named-symbols -o %t.propeller.out
# RUN: llvm-nm -nS %t.propeller.out| FileCheck %s --check-prefix=AFTER

# AFTER:	0000000000201120 0000000000000005 t foo
# AFTER-NEXT:	0000000000201125 0000000000000005 t a.BB.foo
# AFTER-NEXT:	000000000020112a 0000000000000004 t aaa.BB.foo
# AFTER-NEXT:	000000000020112e 0000000000000004 t aa.BB.foo

#.global _start
#_start:
# -- Begin function foo
.section	.text,"ax",@progbits
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
 nopl (%rax)
 ret
.Laa.BB.foo_end:
 .size	aa.BB.foo, .Laa.BB.foo_end-aa.BB.foo

.section	.text,"ax",@progbits,unique,3
aaa.BB.foo:
 nopl (%rax)
 ret
.Laaa.BB.foo_end:
 .size	aaa.BB.foo, .Laaa.BB.foo_end-aaa.BB.foo

.section	.text,"ax",@progbits
.Lfoo_end:
 .size	foo, .Lfoo_end-foo
# -- End function foo
