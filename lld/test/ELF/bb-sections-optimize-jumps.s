# REQUIRES: x86
## basicblock-sections tests.
## This test exercises optimizing the jumps (flipping and removing fall-through jumps)
## at the end of basic blocks on a single function with a simple loop.

# RUN: llvm-mc -filetype=obj -triple=x86_64-pc-linux %s -o %t.o
# RUN: ld.lld  --optimize-bb-jumps %t.o -o %t.out
# RUN: llvm-objdump -d %t.out| FileCheck %s --check-prefix=CHECK

# CHECK:	<foo>
# CHECK-NEXT:	nopl    (%rax)
# CHECK-NEXT:	74 03	je      {{.*}} <aa.BB.foo>

# CHECK:	<a.BB.foo>
# CHECK-NEXT:	nopl    (%rax)

# CHECK:	<aa.BB.foo>
# CHECK-NEXT:	nopl    (%rax)
# CHECK-NEXT:	75 f8	jne     {{.*}} <a.BB.foo>

# CHECK:	<aaa.BB.foo>
# CHECK-NEXT:	nopl    (%rax)
# CHECK-NEXT:	retq


.section	.text,"ax",@progbits
# -- Begin function foo
.type	foo,@function
foo:
 nopl (%rax)
 jne	a.BB.foo
 jmp	aa.BB.foo

.section	.text,"ax",@progbits,unique,2
a.BB.foo:
 nopl (%rax)
 jmp	aa.BB.foo

.section	.text,"ax",@progbits,unique,3
aa.BB.foo:
 nopl (%rax)
 jne	a.BB.foo
 jmp	aaa.BB.foo

.section	.text,"ax",@progbits,unique,4
aaa.BB.foo:
 nopl (%rax)
 ret
