# REQUIRES: x86
## Basic propeller tests.
## This test exercises function reordering, and function splitting on a single function.

# RUN: llvm-mc -filetype=obj -triple=x86_64-pc-linux %s -o %t.o
# RUN: ld.lld  %t.o -o %t.out
# RUN: llvm-nm -Sn %t.out| FileCheck %s --check-prefix=NM1

# NM1:	0000000000201000 0000000000000008 t bar
# NM1:	0000000000201008 0000000000000005 t foo
# NM1:	000000000020100d 0000000000000005 t a.BB.foo
# NM1:	0000000000201012 0000000000000008 t aa.BB.foo


# RUN: llvm-objdump -s %t.out| FileCheck %s --check-prefix=BEFORE

# BEFORE:      Contents of section .text:
# BEFORE-NEXT:  201000 44000000 00000000 0f1f0074 050f1f00
# BEFORE-NEXT:	201010 74fb3300 00000000 0000

## Create a propeller profile for foo and bar based on the cfg below:
##
##
##     foo
##      | \      100
##      |  \0  +-----+
##      |   \  |     |
##      |    v |     |
##    0 | a.BB.foo <-+
##      |    /  |       100
##      |   /0  +---------------> bar
##      |  /
##      v v
##   aa.BB.foo
##

# RUN: echo "Symbols" > %t_prof.propeller
# RUN: echo "1 1a Nfoo" >> %t_prof.propeller
# RUN: echo "2 5 1.1" >> %t_prof.propeller
# RUN: echo "3 8 1.2" >> %t_prof.propeller
# RUN: echo "4 8 Nbar" >> %t_prof.propeller
# RUN: echo "Branches" >> %t_prof.propeller
# RUN: echo "2 2 100" >> %t_prof.propeller
# RUN: echo "2 4 100 C" >> %t_prof.propeller
# RUN: echo "Fallthroughs" >> %t_prof.propeller

# RUN: ld.lld  %t.o -propeller=%t_prof.propeller --verbose -propeller-keep-named-symbols -o %t.propeller.out
# RUN: llvm-nm -n %t.propeller.out| FileCheck %s --check-prefix=NM2

# NM2:	0000000000201000 t foo
# NM2:	0000000000201005 t a.BB.foo
# NM2:	000000000020100c t bar
# NM2:	0000000000201014 t aa.BB.foo

# RUN: llvm-objdump -s %t.propeller.out| FileCheck %s --check-prefix=AFTER

# AFTER:      Contents of section .text
# AFTER-NEXT:  201000 0f1f0074 0f0f1f00 74fbeb08 44000000
# AFTER-NEXT:  201010 00000000 33000000 00000000
#
.section	.text,"ax",@progbits
# -- Begin function bar
.type	bar,@function
bar:
 .quad 0x44

.Lbar_end:
 .size	bar, .Lbar_end-bar
# -- End function bar



.section	.text,"ax",@progbits,unique,1
# -- Begin function foo
.type	foo,@function
foo:
 nopl (%rax)
 je	aa.BB.foo
 jmp	a.BB.foo

.section	.text,"ax",@progbits,unique,2
a.BB.foo:
 nopl (%rax)
 je	a.BB.foo
 jmp	aa.BB.foo
.La.BB.foo_end:
 .size	a.BB.foo, .La.BB.foo_end-a.BB.foo

.section	.text,"ax",@progbits,unique,3
aa.BB.foo:
 .quad	0x33
.Laa.BB.foo_end:
 .size	aa.BB.foo, .Laa.BB.foo_end-aa.BB.foo

.section	.text,"ax",@progbits,unique,1
.Lfoo_end:
 .size	foo, .Lfoo_end-foo
# -- End function foo


