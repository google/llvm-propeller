# REQUIRES: x86
## basicblock-sections tests.
## This simple test checks if redundant direct jumps are converted to
## implicit fallthrus.  The jne must be converted to je and the direct
## jmp must be deleted.

# RUN: llvm-mc -filetype=obj -triple=x86_64-pc-linux %s -o %t.o
# RUN: ld.lld  -optimize-bb-jumps %t.o -o %t.out
# RUN: llvm-objdump -d %t.out| FileCheck %s --check-prefix=CHECK

# CHECK:	foo:
# CHECK-NEXT:	nopl    (%rax)
# CHECK-NEXT:	{{[0-9|a-f| ]*}} je      3 <aa.BB.foo>
# CHECK-NOT:    jmp

# CHECK:	a.BB.foo:

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

aa.BB.foo:
 ret
