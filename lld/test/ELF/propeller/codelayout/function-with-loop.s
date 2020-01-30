# REQUIRES: x86
## Basic propeller tests.
## This test exercises basic block reordering on a single function with a simple loop.

# RUN: llvm-mc -filetype=obj -triple=x86_64-pc-linux %s -o %t.o
# RUN: ld.lld  %t.o -optimize-bb-jumps -o %t.out
# RUN: llvm-objdump -d %t.out| FileCheck %s --check-prefix=BEFORE

# BEFORE:	0000000000201120 foo:
# BEFORE-NEXT:	nopl    (%rax)

# BEFORE:	0000000000201123 a.BB.foo:
# BEFORE-NEXT:	nopl	(%rax)
# BEFORE-NEXT:	je      3 <aaa.BB.foo>

# BEFORE:	0000000000201128 aa.BB.foo:
# BEFORE-NEXT:	nopl    (%rax)

# BEFORE:	000000000020112b aaa.BB.foo:
# BEFORE-NEXT:	nopl    (%rax)
# BEFORE-NEXT:	jne     -13 <a.BB.foo>

# BEFORE:	0000000000201130 aaaa.BB.foo:
# BEFORE-NEXT:	nopl    (%rax)
# BEFORE-NEXT:	retq


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
## The optimal layout must include foo -> a.BB.foo -> aaa.BB.foo -> aaaa.BB.foo
## as a fallthrough chain in the layout.

# RUN: echo "!foo" > %t_prof.propeller
# RUN: echo "!!1" >> %t_prof.propeller
# RUN: echo "!!3" >> %t_prof.propeller
# RUN: echo "!!4" >> %t_prof.propeller
# RUN: echo "Symbols" >> %t_prof.propeller
# RUN: echo "1 14 Nfoo" >> %t_prof.propeller
# RUN: echo "2 5 1.1" >> %t_prof.propeller
# RUN: echo "3 3 1.2" >> %t_prof.propeller
# RUN: echo "4 5 1.3" >> %t_prof.propeller
# RUN: echo "5 4 1.4" >> %t_prof.propeller
# RUN: echo "Branches" >> %t_prof.propeller
# RUN: echo "4 2 90" >> %t_prof.propeller
# RUN: echo "2 4 95" >> %t_prof.propeller
# RUN: echo "Fallthroughs" >> %t_prof.propeller
# RUN: echo "4 5 10" >> %t_prof.propeller
# RUN: echo "1 2 5" >> %t_prof.propeller

# RUN: ld.lld  %t.o -optimize-bb-jumps -propeller=%t_prof.propeller -propeller-keep-named-symbols -o %t.propeller.reorder.out
# RUN: llvm-objdump -d %t.propeller.reorder.out| FileCheck %s --check-prefix=REORDER

# REORDER:	0000000000201120 foo:
# REORDER-NEXT:	nopl	(%rax)

# REORDER:	0000000000201123 a.BB.foo:
# REORDER-NEXT:	nopl	(%rax)
# REORDER-NEXT:	jne      9 <aa.BB.foo>

# REORDER:	0000000000201128 aaa.BB.foo:
# REORDER-NEXT:	nopl    (%rax)
# REORDER-NEXT:	jne     -10 <a.BB.foo>

# REORDER:	000000000020112d aaaa.BB.foo:
# REORDER-NEXT:	nopl    (%rax)
# REORDER-NEXT:	retq

# REORDER:	0000000000201131 aa.BB.foo:
# REORDER-NEXT:	nopl    (%rax)
# REORDER-NEXT:	jmp	-14 <aaa.BB.foo>

## Disable basic block reordering and expect that the original bb order is retained.
#
# RUN: ld.lld  %t.o -optimize-bb-jumps -propeller=%t_prof.propeller -propeller-opt=no-reorder-blocks -propeller-keep-named-symbols -o %t.propeller.noreorder.out
# RUN: diff %t.propeller.noreorder.out %t.out

.section	.text,"ax",@progbits
# -- Begin function foo
.type	foo,@function
foo:
 nopl (%rax)
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
 jmp	aaa.BB.foo
.Laa.BB.foo_end:
 .size	aa.BB.foo, .Laa.BB.foo_end-aa.BB.foo

.section	.text,"ax",@progbits,unique,3
aaa.BB.foo:
 nopl (%rax)
 jne	a.BB.foo
 jmp	aaaa.BB.foo
.Laaa.BB.foo_end:
 .size	aaa.BB.foo, .Laaa.BB.foo_end-aaa.BB.foo

.section	.text,"ax",@progbits,unique,4
aaaa.BB.foo:
 nopl (%rax)
 ret
.Laaaa.BB.foo_end:
 .size	aaaa.BB.foo, .Laaaa.BB.foo_end-aaaa.BB.foo

.section	.text,"ax",@progbits
.Lfoo_end:
 .size	foo, .Lfoo_end-foo
