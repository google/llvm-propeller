# REQUIRES: x86
## basicblock-sections tests.
## This simple test checks if redundant direct jumps are converted to
## implicit fallthrus.  The jcc's must be converted to their inverted
## opcode, for instance jne to je and jmp must be deleted.

# RUN: llvm-mc -filetype=obj -triple=x86_64-pc-linux %s -o %t.o
# RUN: ld.lld  -optimize-bb-jumps %t.o -o %t.out
# RUN: llvm-objdump -d %t.out| FileCheck %s --check-prefix=CHECK

# CHECK:	foo:
# CHECK-NEXT:	nopl    (%rax)
# CHECK-NEXT:	{{[0-9|a-f| ]*}} jne      {{[0-9]+}} <r.BB.foo>
# CHECK-NOT:    jmp


.section	.text,"ax",@progbits
# -- Begin function foo
.type	foo,@function
foo:
 nopl (%rax)
 je	a.BB.foo
 jmp	r.BB.foo

# CHECK:	a.BB.foo:
# CHECK-NEXT:	nopl    (%rax)
# CHECK-NEXT:	{{[0-9|a-f| ]*}} je      {{[0-9]+}} <r.BB.foo>
# CHECK-NOT:    jmp

.section	.text,"ax",@progbits,unique,3
a.BB.foo:
 nopl (%rax)
 jne	aa.BB.foo
 jmp	r.BB.foo

# CHECK:	aa.BB.foo:
# CHECK-NEXT:	nopl    (%rax)
# CHECK-NEXT:	{{[0-9|a-f| ]*}} jle      {{[0-9]+}} <r.BB.foo>
# CHECK-NOT:    jmp
#
.section	.text,"ax",@progbits,unique,4
aa.BB.foo:
 nopl (%rax)
 jg	aaa.BB.foo
 jmp	r.BB.foo

# CHECK:	aaa.BB.foo:
# CHECK-NEXT:	nopl    (%rax)
# CHECK-NEXT:	{{[0-9|a-f| ]*}} jl      {{[0-9]+}} <r.BB.foo>
# CHECK-NOT:    jmp
#
.section	.text,"ax",@progbits,unique,5
aaa.BB.foo:
 nopl (%rax)
 jge	aaaa.BB.foo
 jmp	r.BB.foo

# CHECK:	aaaa.BB.foo:
# CHECK-NEXT:	nopl    (%rax)
# CHECK-NEXT:	{{[0-9|a-f| ]*}} jae      {{[0-9]+}} <r.BB.foo>
# CHECK-NOT:    jmp
#
.section	.text,"ax",@progbits,unique,6
aaaa.BB.foo:
 nopl (%rax)
 jb	aaaaa.BB.foo
 jmp	r.BB.foo

# CHECK:	aaaaa.BB.foo:
# CHECK-NEXT:	nopl    (%rax)
# CHECK-NEXT:	{{[0-9|a-f| ]*}} ja      {{[0-9]+}} <r.BB.foo>
# CHECK-NOT:    jmp
#
.section	.text,"ax",@progbits,unique,7
aaaaa.BB.foo:
 nopl (%rax)
 jbe	aaaaaa.BB.foo
 jmp	r.BB.foo

# CHECK:	aaaaaa.BB.foo:
# CHECK-NEXT:	nopl    (%rax)
# CHECK-NEXT:	{{[0-9|a-f| ]*}} jge      {{[0-9]+}} <r.BB.foo>
# CHECK-NOT:    jmp
#
.section	.text,"ax",@progbits,unique,8
aaaaaa.BB.foo:
 nopl (%rax)
 jl	aaaaaaa.BB.foo
 jmp	r.BB.foo

# CHECK:	aaaaaaa.BB.foo:
# CHECK-NEXT:	nopl    (%rax)
# CHECK-NEXT:	{{[0-9|a-f| ]*}} jg      {{[0-9]+}} <r.BB.foo>
# CHECK-NOT:    jmp
#
.section	.text,"ax",@progbits,unique,9
aaaaaaa.BB.foo:
 nopl (%rax)
 jle	aaaaaaaa.BB.foo
 jmp	r.BB.foo

# CHECK:	aaaaaaaa.BB.foo:
# CHECK-NEXT:	nopl    (%rax)
# CHECK-NEXT:	{{[0-9|a-f| ]*}} jbe      {{[0-9]+}} <r.BB.foo>
# CHECK-NOT:    jmp
#
.section	.text,"ax",@progbits,unique,10
aaaaaaaa.BB.foo:
 nopl (%rax)
 ja	aaaaaaaaa.BB.foo
 jmp	r.BB.foo

# CHECK:	aaaaaaaaa.BB.foo:
# CHECK-NEXT:	nopl    (%rax)
# CHECK-NEXT:	{{[0-9|a-f| ]*}} jb      {{[0-9]+}} <r.BB.foo>
# CHECK-NOT:    jmp
#
.section	.text,"ax",@progbits,unique,11
aaaaaaaaa.BB.foo:
 nopl (%rax)
 jae	aaaaaaaaaa.BB.foo
 jmp	r.BB.foo

.section	.text,"ax",@progbits,unique,20
aaaaaaaaaa.BB.foo:
 nopl (%rax)

r.BB.foo:
 ret
