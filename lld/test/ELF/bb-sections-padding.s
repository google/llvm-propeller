# REQUIRES: x86
# This test exercises padding of basic block sections with nop and trap instructions.
# A section will be padded by nops when it has a fallthrough to the next basic block.
#
# RUN: llvm-mc -filetype=obj -triple=x86_64-pc-linux %s -o %t.o
# RUN: ld.lld --optimize-bb-jumps %t.o -o %t.out
# RUN: llvm-objdump -d %t.out| FileCheck %s

# CHECK:	0000000000201140 <_foo1>
# CHECK-NEXT:   nop
# CHECK-NEXT:   nopl	(%rax)
# CHECK-EMPTY:
# CHECK-NEXT:	0000000000201148 <_foo2>
# CHECK-NEXT:	nop
# CHECK-NEXT:	nop
# CHECK-NEXT:	nopw	(%rax,%rax)
# CHECK-EMPTY:
# CHECK-NEXT:	0000000000201150 <_foo3>
# CHECK-NEXT:	nop
# CHECK-NEXT:	nop
# CHECK-NEXT:	nop
# CHECK-NEXT:	nopw	(%rax,%rax)
# CHECK-NEXT:   nopl	(%rax)
# CHECK-EMPTY:
# CHECK-NEXT:	0000000000201160 <_foo4>
# CHECK-NEXT:	nop
# CHECK-NEXT:	nop
# CHECK-NEXT:	nop
# CHECK-NEXT:	nop
# CHECK-NEXT:	nopw	(%rax,%rax)
# CHECK-NEXT:	nopw	(%rax,%rax)
# CHECK-NEXT:	nopw	(%rax,%rax)
# CHECK-NEXT:	nop
# CHECK-EMPTY:
# CHECK-NEXT:	0000000000201180 <_foo5>
# CHECK-NEXT:	jmp 0x201188 <_foo7>
# CHECK-NEXT:	int3
# CHECK-NEXT:	int3
# CHECK-EMPTY:
# CHECK-NEXT:	0000000000201184 <_foo6>
# CHECK-NEXT:	retq
# CHECK-NEXT:	int3
# CHECK-NEXT:	int3
# CHECK-NEXT:	int3
# CHECK-EMPTY:
# CHECK-NEXT:	0000000000201188 <_foo7>

.section .foo,"ax",@progbits,unique,1
_foo1:
 nop
 jmp    _foo2

.section .foo,"ax",@progbits,unique,2
.align 8
_foo2:
 nop
 nop
 jmp	_foo3

.section .foo,"ax",@progbits,unique,3
.align 16
_foo3:
 nop
 nop
 nop
 jmp	_foo4

.section .foo,"ax",@progbits,unique,4
.align 32
_foo4:
 nop
 nop
 nop
 nop
 jmp	_foo5

.section .foo,"ax",@progbits,unique,5
.align 64
_foo5:
 jmp	_foo7

.section .foo,"ax",@progbits,unique,6
.align 4
_foo6:
 ret

.section .foo,"ax",@progbits,unique,7
.align 4
_foo7:
 ret
