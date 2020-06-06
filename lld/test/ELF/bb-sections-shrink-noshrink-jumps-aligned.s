# REQUIRES: x86
## basicblock-sections tests.
## This test exercises shrinking the jumps at the end of basic blocks
## on a single function. This test has been articulated such that it
## the shrinking of the conditional branch in the first basic block
## results in increasing the branch offset of another jump going from
## 127 to 130. This means the latter branch cannot be shrunk, which the
## test asserts.

# RUN: llvm-mc -filetype=obj -triple=x86_64-pc-linux %s -o %t.o
# RUN: ld.lld --optimize-bb-jumps %t.o -o %t.out
# RUN: llvm-objdump -d %t.out| FileCheck %s --check-prefix=CHECK

# CHECK:	<foo>
# CHECK-NEXT:	nopl    (%rax)
# CHECK-NEXT:	75 11		jne	{{.*}} <a.BB.foo>
# CHECK-NEXT:	e9 96 00 00 00	jmp	{{.*}} <aa.BB.foo>
# CHECK-EMPTY:

# CHECK:	<a.BB.foo>
# CHECK-NEXT:	nopl    (%rax)
# CHECK-NEXT:	nopl    (%rax)
# CHECK-NEXT:	e9 7f 00 00 00	jmp	{{.*}} <aa.BB.foo>
# CHECK-EMPTY:

# CHECK:	<aa.BB.foo>
# CHECK-NEXT:	nopl    (%rax)
# CHECK-NEXT:	nopl    (%rax)
# CHECK-NEXT:	nopl    (%rax)
# CHECK-NEXT:	retq
# CHECK-EMPTY:

.section	.text,"ax",@progbits
# -- Begin function foo
.type	foo,@function
.align 128
foo:
 nopl (%rax)
 jne	a.BB.foo
 jmp	aa.BB.foo

.section	.text.1,"ax",@progbits,unique,1
zeros.1:
 .zero	12

.section	.text.2,"ax",@progbits,unique,2
a.BB.foo:
 nopl (%rax)
 nopl (%rax)
 jmp	aa.BB.foo

.section	.text.3,"ax",@progbits,unique,3
zeros.2:
 .zero	115

.section	.text.4,"ax",@progbits,unique,4
.align 16
aa.BB.foo:
 nopl (%rax)
 nopl (%rax)
 nopl (%rax)
 ret
