# REQUIRES: x86
## Basic propeller tests.
## This test exercises all combinations of propeller options (reorder-funcs, reorder-blocks,
## and split-funcs) on four functions.

# RUN: llvm-mc -filetype=obj -triple=x86_64-pc-linux %s -o %t.o
# RUN: ld.lld %t.o -optimize-bb-jumps -o %t.out
# RUN: llvm-objdump -d %t.out| FileCheck %s --check-prefix=BEFORE

# BEFORE:	Disassembly of section .text:
# BEFORE-EMPTY:
# BEFORE-NEXT:	foo:
# BEFORE-NEXT:	xorb	%al, 0
# BEFORE-NEXT:	int3
# BEFORE-EMPTY:
# BEFORE-NEXT:	bar:
# BEFORE-NEXT:	xorb	%al, 1
# BEFORE-NEXT:	je	9 <aa.BB.bar>
# BEFORE-EMPTY:
# BEFORE-NEXT:	a.BB.bar:
# BEFORE-NEXT:	xorb	%al, 2
# BEFORE-NEXT:	jmp      7 <aaa.BB.bar>
# BEFORE-EMPTY:
# BEFORE-NEXT:	aa.BB.bar:
# BEFORE-NEXT:	xorb	%al, 3
# BEFORE-EMPTY:
# BEFORE-NEXT:	aaa.BB.bar:
# BEFORE-NEXT:	xorb	%al, 4
# BEFORE-EMPTY:
# BEFORE-NEXT:	baz:
# BEFORE-NEXT:	xorb	%al, 5
# BEFORE-NEXT:	int3
# BEFORE-EMPTY:
# BEFORE-NEXT:	qux:
# BEFORE-NEXT:	xorb	%al, 6
#

## Create a propeller profile for four functions foo, bar, baz, and qux based on the cfg below:
##
##
##                   bar
##                  /   \
##                 /     \100
##                /       \
##       100     v         v
## foo <----- a.BB.bar  aa.BB.bar     baz     qux ---+
##               \        /                    ^     |
##                \      /100                  |   1 |
##                 \    /                      +-----+
##                  v  v
##                aaa.BB.bar
##

# RUN: echo "!foo" > %t_prof.propeller
# RUN: echo "!bar" >> %t_prof.propeller
# RUN: echo "!!1" >> %t_prof.propeller
# RUN: echo "!!2" >> %t_prof.propeller
# RUN: echo "!!3" >> %t_prof.propeller
# RUN: echo "!qux" >> %t_prof.propeller
# RUN: echo "Symbols" >> %t_prof.propeller
# RUN: echo "1 8 Nfoo" >> %t_prof.propeller
# RUN: echo "2 20 Nbar" >> %t_prof.propeller
# RUN: echo "3 9 2.1" >> %t_prof.propeller
# RUN: echo "4 7 2.2" >> %t_prof.propeller
# RUN: echo "5 7 2.3" >> %t_prof.propeller
# RUN: echo "6 8 Nbaz" >> %t_prof.propeller
# RUN: echo "7 7 Nqux" >> %t_prof.propeller
# RUN: echo "Branches" >> %t_prof.propeller
# RUN: echo "2 4 100" >> %t_prof.propeller
# RUN: echo "4 1 100 C" >> %t_prof.propeller
# RUN: echo "7 7 10 C" >> %t_prof.propeller
# RUN: echo "Fallthroughs" >> %t_prof.propeller
# RUN: echo "4 5 100" >> %t_prof.propeller

# RUN: ld.lld  %t.o -optimize-bb-jumps -propeller=%t_prof.propeller --verbose -propeller-keep-named-symbols -o %t.propeller.reorder.out
# RUN: llvm-objdump -d %t.propeller.reorder.out| FileCheck %s --check-prefix=REORDER

# REORDER:	Disassembly of section .text:
# REORDER-EMPTY:
# REORDER-NEXT:	bar:
# REORDER-NEXT: xorb	%al, 1
# REORDER-NEXT:	jne	30 <a.BB.bar>
# REORDER-EMPTY:
# REORDER-NEXT:	aa.BB.bar:
# REORDER-NEXT:	xorb	%al, 3
# REORDER-EMPTY:
# REORDER-NEXT:	aaa.BB.bar:
# REORDER-NEXT:	xorb	%al, 4
# REORDER-NEXT:	int3
# REORDER-EMPTY:
# REORDER-NEXT:	foo:
# REORDER-NEXT:	xorb	%al, 0
# REORDER-NEXT:	int3
# REORDER-EMPTY:
# REORDER-NEXT:	qux:
# REORDER-NEXT:	xorb	%al, 6
# REORDER-EMPTY:
# REORDER-NEXT:	a.BB.bar:
# REORDER-NEXT:	xorb	%al, 2
# REORDER-NEXT:	jmp	-32 <aaa.BB.bar>
# REORDER-EMPTY:
# REORDER-NEXT:	baz:
# REORDER-NEXT:	xorb	%al, 5
#

# RUN: ld.lld  %t.o -optimize-bb-jumps -propeller=%t_prof.propeller -propeller-opt=no-reorder-blocks -propeller-keep-named-symbols -o %t.propeller.noreorderblocks.out  2>&1 | FileCheck %s --check-prefixes=WARN,IMPLICITNOSPLIT
# WARN-NOT:		warning:
# IMPLICITNOSPLIT:	warning: propeller: no-reorder-blocks implicitly sets no-split-funcs.
#
# RUN: llvm-objdump -d %t.propeller.noreorderblocks.out| FileCheck %s --check-prefix=NO_REORDER_BB
#
# NO_REORDER_BB:		Disassembly of section .text:
# NO_REORDER_BB-EMPTY:
# NO_REORDER_BB-NEXT:	bar:
# NO_REORDER_BB-NEXT:	xorb	%al, 1
# NO_REORDER_BB-NEXT:	je	9 <aa.BB.bar>
# NO_REORDER_BB-EMPTY:
# NO_REORDER_BB-NEXT:	a.BB.bar:
# NO_REORDER_BB-NEXT:	xorb	%al, 2
# NO_REORDER_BB-NEXT:	jmp	7 <aaa.BB.bar>
# NO_REORDER_BB-EMPTY:
# NO_REORDER_BB-NEXT:	aa.BB.bar:
# NO_REORDER_BB-NEXT:	xorb	%al, 3
# NO_REORDER_BB-EMPTY:
# NO_REORDER_BB-NEXT:	aaa.BB.bar:
# NO_REORDER_BB-NEXT:	xorb	%al, 4
# NO_REORDER_BB-EMPTY:
# NO_REORDER_BB-NEXT:	foo:
# NO_REORDER_BB-NEXT:	xorb	%al, 0
# NO_REORDER_BB-NEXT:	int3
# NO_REORDER_BB-EMPTY:
# NO_REORDER_BB-NEXT:	qux:
# NO_REORDER_BB-NEXT:	xorb	%al, 6
# NO_REORDER_BB-NEXT:	int3
# NO_REORDER_BB-EMPTY:
# NO_REORDER_BB-NEXT:	baz:
# NO_REORDER_BB-NEXT:	xorb	%al, 5

# RUN: ld.lld  %t.o -optimize-bb-jumps -propeller=%t_prof.propeller -propeller-opt=no-reorder-funcs -propeller-keep-named-symbols -o %t.propeller.noreorderfuncs.out
# RUN: llvm-objdump -d %t.propeller.noreorderfuncs.out| FileCheck %s --check-prefix=NO_REORDER_FUNC
#
# NO_REORDER_FUNC:	Disassembly of section .text:
# NO_REORDER_FUNC-EMPTY:
# NO_REORDER_FUNC-NEXT:	foo:
# NO_REORDER_FUNC-NEXT:	xorb	%al, 0
# NO_REORDER_FUNC-NEXT:	int3
# NO_REORDER_FUNC-EMPTY:
# NO_REORDER_FUNC-NEXT:	bar:
# NO_REORDER_FUNC-NEXT: xorb	%al, 1
# NO_REORDER_FUNC-NEXT:	jne	22 <a.BB.bar>
# NO_REORDER_FUNC-EMPTY:
# NO_REORDER_FUNC-NEXT:	aa.BB.bar:
# NO_REORDER_FUNC-NEXT: xorb	%al, 3
# NO_REORDER_FUNC-EMPTY:
# NO_REORDER_FUNC-NEXT:	aaa.BB.bar:
# NO_REORDER_FUNC-NEXT: xorb	%al, 4
# NO_REORDER_FUNC-NEXT:	int3
# NO_REORDER_FUNC-EMPTY:
# NO_REORDER_FUNC-NEXT:	qux:
# NO_REORDER_FUNC-NEXT:	xorb	%al, 6
# NO_REORDER_FUNC-EMPTY:
# NO_REORDER_FUNC-NEXT:	a.BB.bar:
# NO_REORDER_FUNC-NEXT: xorb	%al, 2
# NO_REORDER_FUNC-NEXT:	jmp	-24 <aaa.BB.bar>
# NO_REORDER_FUNC-EMPTY:
# NO_REORDER_FUNC-NEXT:	baz:
# NO_REORDER_FUNC-NEXT:	xorb	%al, 5
#

# RUN: ld.lld  %t.o -optimize-bb-jumps -propeller=%t_prof.propeller -propeller-opt=no-split-funcs -propeller-keep-named-symbols -o %t.propeller.nosplitfuncs.out
# RUN: llvm-objdump -d %t.propeller.nosplitfuncs.out| FileCheck %s --check-prefix=NO_SPLIT_FUNC
#
# NO_SPLIT_FUNC:	Disassembly of section .text:
# NO_SPLIT_FUNC-EMPTY:
# NO_SPLIT_FUNC-NEXT:	bar:
# NO_SPLIT_FUNC-NEXT: 	xorb	%al, 1
# NO_SPLIT_FUNC-NEXT:	jne	14 <a.BB.bar>
# NO_SPLIT_FUNC-EMPTY:
# NO_SPLIT_FUNC-NEXT:	aa.BB.bar:
# NO_SPLIT_FUNC-NEXT: 	xorb	%al, 3
# NO_SPLIT_FUNC-EMPTY:
# NO_SPLIT_FUNC-NEXT:	aaa.BB.bar:
# NO_SPLIT_FUNC-NEXT: 	xorb	%al, 4
# NO_SPLIT_FUNC-EMPTY:
# NO_SPLIT_FUNC-NEXT:	a.BB.bar:
# NO_SPLIT_FUNC-NEXT: 	xorb	%al, 2
# NO_SPLIT_FUNC-NEXT:	jmp	-16 <aaa.BB.bar>
# NO_SPLIT_FUNC-EMPTY:
# NO_SPLIT_FUNC-NEXT:	foo:
# NO_SPLIT_FUNC-NEXT: 	xorb	%al, 0
# NO_SPLIT_FUNC-NEXT:	int3
# NO_SPLIT_FUNC-EMPTY:
# NO_SPLIT_FUNC-NEXT:	qux:
# NO_SPLIT_FUNC-NEXT:	xorb	%al, 6
# NO_SPLIT_FUNC-NEXT:	int3
# NO_SPLIT_FUNC-EMPTY:
# NO_SPLIT_FUNC-NEXT:	baz:
# NO_SPLIT_FUNC-NEXT:	xorb	%al, 5
#

## Check that the combination of no-reorder-blocks and split-funcs makes lld fail.
# RUN: not ld.lld  %t.o -propeller=%t_prof.propeller -propeller-opt=no-reorder-blocks -propeller-opt=split-funcs 2>&1 -o /dev/null | FileCheck %s --check-prefix=FAIL
# FAIL:		propeller: Inconsistent combination of propeller optimizations 'split-funcs' and 'no-reorder-blocks'.


# RUN: ld.lld  %t.o -optimize-bb-jumps -propeller=%t_prof.propeller -propeller-opt=no-split-funcs -propeller-opt=no-reorder-funcs -propeller-keep-named-symbols -o %t.propeller.nosplitreorderfuncs.out
# RUN: llvm-objdump -d %t.propeller.nosplitreorderfuncs.out| FileCheck %s --check-prefix=NO_SPLIT_REORDER_FUNC
#
# NO_SPLIT_REORDER_FUNC:	Disassembly of section .text:
# NO_SPLIT_REORDER_FUNC-EMPTY:
# NO_SPLIT_REORDER_FUNC-NEXT:	foo:
# NO_SPLIT_REORDER_FUNC-NEXT:	xorb	%al, 0
# NO_SPLIT_REORDER_FUNC-NEXT:	int3
# NO_SPLIT_REORDER_FUNC-EMPTY:
# NO_SPLIT_REORDER_FUNC-NEXT:	bar:
# NO_SPLIT_REORDER_FUNC-NEXT: 	xorb	%al, 1
# NO_SPLIT_REORDER_FUNC-NEXT:	jne	14 <a.BB.bar>
# NO_SPLIT_REORDER_FUNC-EMPTY:
# NO_SPLIT_REORDER_FUNC-NEXT:	aa.BB.bar:
# NO_SPLIT_REORDER_FUNC-NEXT: 	xorb	%al, 3
# NO_SPLIT_REORDER_FUNC-EMPTY:
# NO_SPLIT_REORDER_FUNC-NEXT:	aaa.BB.bar:
# NO_SPLIT_REORDER_FUNC-NEXT: 	xorb	%al, 4
# NO_SPLIT_REORDER_FUNC-EMPTY:
# NO_SPLIT_REORDER_FUNC-NEXT:	a.BB.bar:
# NO_SPLIT_REORDER_FUNC-NEXT: 	xorb	%al, 2
# NO_SPLIT_REORDER_FUNC-NEXT:	jmp	-16 <aaa.BB.bar>
# NO_SPLIT_REORDER_FUNC-EMPTY:
# NO_SPLIT_REORDER_FUNC-NEXT:	baz:
# NO_SPLIT_REORDER_FUNC-NEXT:	xorb	%al, 5
# NO_SPLIT_REORDER_FUNC-NEXT:	int3
# NO_SPLIT_REORDER_FUNC-EMPTY:
# NO_SPLIT_REORDER_FUNC-NEXT:	qux:
# NO_SPLIT_REORDER_FUNC-NEXT:	xorb	%al, 6

.section	.text,"ax",@progbits
# -- Begin function foo
.type	foo,@function
.align	4
foo:
 xor %al,0

.Lfoo_end:
 .size	foo, .Lfoo_end-foo
# -- End function foo

.section	.text,"ax",@progbits,unique,1
# -- Begin function bar
.type	bar,@function
.align	4
bar:
 xor %al,1
 je	aa.BB.bar
 jmp	a.BB.bar

.section	.text,"ax",@progbits,unique,2
a.BB.bar:
 xor %al,2
 jmp	aaa.BB.bar
.La.BB.bar_end:
 .size	a.BB.bar, .La.BB.bar_end-a.BB.bar

.section	.text,"ax",@progbits,unique,3
aa.BB.bar:
 xor %al,3
 jmp	aaa.BB.bar
.Laa.BB.bar_end:
 .size	aa.BB.bar, .Laa.BB.bar_end-aa.BB.bar

.section	.text,"ax",@progbits,unique,4
aaa.BB.bar:
 xor %al,4
.Laaa.BB.bar_end:
 .size	aaa.BB.bar, .Laaa.BB.bar_end-aaa.BB.bar

.section	.text,"ax",@progbits,unique,1
.Lbar_end:
 .size	bar, .Lbar_end-bar
# -- End function bar

.section	.text,"ax",@progbits,unique,5
# -- Begin function baz
.type	baz,@function
.align	4
baz:
 xor %al,5

.Lbaz_end:
 .size	baz, .Lbaz_end-baz
# -- End function baz

.section	.text,"ax",@progbits,unique,6
# -- Begin function qux
.type	qux,@function
.align	4
qux:
 xor %al,6

.Lqux_end:
 .size	qux, .Lqux_end-qux
# -- End function qux
