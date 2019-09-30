# REQUIRES: x86
## Basic propeller tests.
## This test exercises function reordering on three functions.

# RUN: llvm-mc -filetype=obj -triple=x86_64 %s -o %t.o
# RUN: ld.lld  %t.o -o %t.out

# RUN: llvm-nm -nS %t.out | FileCheck %s --check-prefix=BEFORE

# BEFORE:	0000000000201120 0000000000000008 t foo
# BEFORE-NEXT:	0000000000201128 0000000000000008 t bar
# BEFORE-NEXT:	0000000000201130 0000000000000008 t baz

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

## Link with the propeller profile and expect the functions to be reordered.
#
# RUN: ld.lld  %t.o -propeller=%t_prof.propeller -o %t.propeller.reorder.out
# RUN: llvm-nm -nS %t.propeller.reorder.out| FileCheck %s --check-prefix=REORDER

# REORDER:	0000000000201120 0000000000000008 t baz
# REORDER-NEXT:	0000000000201128 0000000000000008 t bar
# REORDER-NEXT:	0000000000201130 0000000000000008 t foo

## Disable function reordering and expect that the original function order is retained.
#
# RUN: ld.lld  %t.o -propeller=%t_prof.propeller -propeller-opt=no-reorder-funcs -o %t.propeller.noreorder.out
# RUN: llvm-nm -nS %t.propeller.noreorder.out| FileCheck %s --check-prefix=NOREORDER

# NOREORDER:		0000000000201120 0000000000000008 t foo
# NOREORDER-NEXT:	0000000000201128 0000000000000008 t bar
# NOREORDER-NEXT:	0000000000201130 0000000000000008 t baz

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
