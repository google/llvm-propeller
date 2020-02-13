# REQUIRES: x86
## basicblock-sections tests.
## This simple test checks foo is folded into bar with bb sections
## and the jumps are deletd.

# RUN: llvm-mc -filetype=obj -triple=x86_64-pc-linux %s -o %t.o
# RUN: ld.lld  -optimize-bb-jumps --icf=all %t.o -o %t.out
# RUN: llvm-objdump -d %t.out| FileCheck %s --check-prefix=CHECK

# CHECK:	foo:
# CHECK-NEXT:	nopl    (%rax)
# CHECK-NEXT:	{{[0-9|a-f| ]*}} je      3 <aa.BB.foo>
# CHECK-NOT:    jmp

# CHECK:	a.BB.foo:
# Explicity check that bar is folded and not emitted.
# CHECK-NOT:    bar:
# CHECK-NOT:    a.BB.bar:
# CHECK-NOT:    aa.BB.bar:

.section	.text.bar,"ax",@progbits
# -- Begin function bar
.type	bar,@function
bar:
 nopl (%rax)
 jne	a.BB.bar
 jmp	aa.BB.bar

.section	.text.a.BB.bar,"ax",@progbits,unique,3
a.BB.bar:
 nopl (%rax)

aa.BB.bar:
 ret

.section	.text.foo,"ax",@progbits
# -- Begin function foo
.type	foo,@function
foo:
 nopl (%rax)
 jne	a.BB.foo
 jmp	aa.BB.foo

.section	.text.a.BB.foo,"ax",@progbits,unique,2
a.BB.foo:
 nopl (%rax)

aa.BB.foo:
 ret
