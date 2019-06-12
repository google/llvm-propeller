# REQUIRES: x86
## Basic propeller tests.
## This test exercises function reordering on three functions.

# RUN: llvm-mc -filetype=obj -triple=x86_64-pc-linux %s -o %t.o
# RUN: ld.lld  %t.o -o %t.out
# RUN: llvm-nm -n %t.out| FileCheck %s --check-prefix=NM1

# NM1:	0000000000201000 T foo
# NM1:	0000000000201008 T bar
# NM1:	0000000000201010 T baz

# RUN: llvm-objdump -s %t.out| FileCheck %s --check-prefix=BEFORE

# BEFORE:      Contents of section .foo:
# BEFORE-NEXT:  201000 11000000 00000000
# BEFORE:      Contents of section .bar:
# BEFORE-NEXT:  201008 22000000 00000000
# BEFORE:      Contents of section .baz:
# BEFORE-NEXT:  201010 33000000 00000000

## Create a propeller profile for foo, based on the cfg below:
##
##       100        100
##  baz -----> bar -----> foo
##

# RUN: echo "Symbols" > %t_prof.propeller
# RUN: echo "1 8 Nfoo" >> %t_prof.propeller
# RUN: echo "2 8 Nbar" >> %t_prof.propeller
# RUN: echo "3 8 Nbaz" >> %t_prof.propeller
# RUN: echo "Branches" >> %t_prof.propeller
# RUN: echo "3 2 100 C" >> %t_prof.propeller
# RUN: echo "2 1 100 C" >> %t_prof.propeller
# RUN: echo "Fallthroughs" >> %t_prof.propeller

# RUN: ld.lld  %t.o -propeller=%t_prof.propeller --verbose -o %t.propeller.out
# RUN: llvm-nm -n %t.propeller.out| FileCheck %s --check-prefix=NM2

# NM2:	0000000000201000 T baz
# NM2:	0000000000201008 T bar
# NM2:	0000000000201010 T foo

# RUN: llvm-objdump -s %t.propeller.out| FileCheck %s --check-prefix=AFTER

# AFTER:      Contents of section .baz:
# AFTER-NEXT:  201000 00000011
# AFTER:      Contents of section .bar:
# AFTER-NEXT:  201000 00000022
# AFTER:      Contents of section .foo:
# AFTER-NEXT:  201000 00000033
#

.section	.foo,"ax",@progbits
# -- Begin function foo
.type	foo,@function
.globl   foo
foo:
 .quad 0x11

.Lfoo_end:
 .size	foo, .Lfoo_end-foo
# -- End function foo

.section	.bar,"ax",@progbits
# -- Begin function bar
.type	bar,@function
.globl   bar
bar:
 .quad 0x22

.Lbar_end:
 .size	bar, .Lbar_end-bar
# -- End function bar

.section	.baz,"ax",@progbits
# -- Begin function baz
.type	baz,@function
.globl   baz
baz:
 .quad 0x33

.Lbaz_end:
 .size	baz, .Lbaz_end-baz
# -- End function baz
