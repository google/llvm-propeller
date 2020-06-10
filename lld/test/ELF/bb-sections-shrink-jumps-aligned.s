# REQUIRES: x86
## basicblock-sections tests.
## This test exercises shrinking the short jumps at the end of basic blocks
## on a single function, which has alignment and padding in between.

# RUN: llvm-mc -filetype=obj -triple=x86_64-pc-linux %s -o %t.o
# RUN: ld.lld --optimize-bb-jumps %t.o -o %t.out
# RUN: llvm-objdump -d %t.out| FileCheck %s --check-prefix=CHECK

# CHECK:		<foo>
# CHECK-NEXT:		nopl    (%rax)
# CHECK-NEXT:		75 7b	jne	{{.*}} <a.BB.foo>
# CHECK-NEXT:		e9 b6 00 00 00	jmp	{{.*}} <aa.BB.foo>
# CHECK-COUNT-54:	int3
# CHECK-NOT:		int3
# CHECK-EMPTY:

# CHECK:		<a.BB.foo>
# CHECK-NEXT:		nopl    (%rax)
# CHECK-NEXT:		nopl    (%rax)
# CHECK-COUNT-6:	nopw	(%rax,%rax)
# CHECK-NOT:		nopw	(%rax,%rax)
# CHECK-NEXT:		nopl	(%rax)
# CHECK-EMPTY:

# CHECK:		<aa.BB.foo>
# CHECK-NEXT:		nopl    (%rax)
# CHECK-NEXT:		nopl    (%rax)
# CHECK-NEXT:		nopl    (%rax)
# CHECK-NEXT:		75 b5	jne     {{.*}} <a.BB.foo>
# CHECK-EMPTY:

# CHECK:	<aaa.BB.foo>
# CHECK-NEXT:	nopl    (%rax)
# CHECK-NEXT:	nopl    (%rax)
# CHECK-NEXT:	nopl    (%rax)
# CHECK-NEXT:	nopl    (%rax)
# CHECK-NEXT:	retq
# CHECK-EMPTY:


.section	.text,"ax",@progbits
# -- Begin function foo
.type	foo,@function
.align 64
foo:
 nopl (%rax)
 jne	a.BB.foo
 jmp	aa.BB.foo

.section	.text.1,"ax",@progbits,unique,1
.align 64
zeros.1:
 .zero	64

.section	.text.2,"ax",@progbits,unique,2
a.BB.foo:
 nopl (%rax)
 nopl (%rax)
 jmp	aa.BB.foo

.section	.text.4,"ax",@progbits,unique,3
.align 64
aa.BB.foo:
 nopl (%rax)
 nopl (%rax)
 nopl (%rax)
 jne	a.BB.foo
 jmp	aaa.BB.foo

.section	.text.5,"ax",@progbits,unique,4
aaa.BB.foo:
 nopl (%rax)
 nopl (%rax)
 nopl (%rax)
 nopl (%rax)
 ret
