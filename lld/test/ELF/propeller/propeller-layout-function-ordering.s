# REQUIRES: x86
## Basic propeller tests.
## This test exercises function reordering on three functions.

# RUN: llvm-mc -filetype=obj -triple=x86_64-pc-linux %s -o %t.o
# RUN: ld.lld  %t.o -o %t.out
# RUN: llvm-nm -n %t.out| FileCheck %s --check-prefix=NM1

# NM1:	0000000000201000 t foo
# NM1:	0000000000201008 t bar
# NM1:	0000000000201010 t baz

# RUN: llvm-objdump -s %t.out| FileCheck %s --check-prefix=BEFORE

# BEFORE:      Contents of section .text:
# BEFORE-NEXT:  201000 11000000 00000000 22000000 00000000
# BEFORE-NEXT:  201010 33000000 00000000

## Create a propeller profile based on the following inter-procedural calls.
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

# RUN: ld.lld  %t.o -propeller=%t_prof.propeller -o %t.propeller.out
# RUN: llvm-nm -n %t.propeller.out| FileCheck %s --check-prefix=NM2

# NM2:	0000000000201000 t baz
# NM2:	0000000000201008 t bar
# NM2:	0000000000201010 t foo

# RUN: llvm-objdump -s %t.propeller.out| FileCheck %s --check-prefix=AFTER

# AFTER:      Contents of section .text
# AFTER-NEXT:  201000 33000000 00000000 22000000 0000000
# AFTER-NEXT:  201010 11000000 00000000
#

## Disable function reordering and expect that the original function order is retained.
#
# RUN: ld.lld  %t.o -propeller=%t_prof.propeller -propeller-opt=no-reorder-funcs -o %t.propeller.out
# RUN: llvm-nm -n %t.propeller.out| FileCheck %s --check-prefix=NM3

# NM3:	0000000000201000 t foo
# NM3:	0000000000201008 t bar
# NM3:	0000000000201010 t baz
#

.section	.text,"ax",@progbits,unique,1
# -- Begin function foo
.type	foo,@function
foo:
 .quad 0x11

.Lfoo_end:
 .size	foo, .Lfoo_end-foo
# -- End function foo

.section	.text,"ax",@progbits,unique,2
# -- Begin function bar
.type	bar,@function
bar:
 .quad 0x22

.Lbar_end:
 .size	bar, .Lbar_end-bar
# -- End function bar

.section	.text,"ax",@progbits,unique,3
# -- Begin function baz
.type	baz,@function
baz:
 .quad 0x33

.Lbaz_end:
 .size	baz, .Lbaz_end-baz
# -- End function baz
