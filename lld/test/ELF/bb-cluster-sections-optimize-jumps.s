# REQUIRES: x86
# This test exercises jump optimization for basic block sections
# when multiple basic blocks are emitted into the same section.
# If a BB label is emitted at the optimized part of the section,
# no optimization should be performed.

# RUN: llvm-mc -filetype=obj -triple=x86_64-pc-linux %s -o %t.o
# RUN: ld.lld  --optimize-bb-jumps %t.o -o %t.out
# RUN: llvm-objdump -d %t.out| FileCheck %s --check-prefix=CHECK

# CHECK:	<foo>
# CHECK-NEXT:	nopl    (%rax)
# CHECK-NEXT:	0f 85 05 00 00 00	jne {{.*}} <aa.BB.foo>

# CHECK:	<a.BB.foo>:
# CHECK-NEXT:	e9 05 00 00 00		jmp {{.*}} <bar>

# CHECK:	<aa.BB.foo>
# CHECK-NEXT:	nopl    (%rax)
# CHECK-NEXT:	eb f6	jmp {{.*}} <a.BB.foo>

# CHECK:	<bar>
# CHECK-NEXT:	nopl    (%rax)
# CHECK-NEXT:	retq


.section	.text,"ax",@progbits
# -- Begin function foo
.type	foo,@function
foo:
 nopl (%rax)
 jne	aa.BB.foo

a.BB.foo:
 jmp	bar

.section	.text,"ax",@progbits,unique,2
aa.BB.foo:
 nopl (%rax)
 jmp	a.BB.foo

.section	.text,"ax",@progbits,unique,3
bar:
 nopl (%rax)
 ret
