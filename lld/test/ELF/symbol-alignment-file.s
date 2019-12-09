# REQUIRES: x86
# RUN: llvm-mc -filetype=obj -triple=x86_64-pc-linux %s -o %t.o
# RUN: ld.lld %t.o -o %t.out
# RUN: llvm-objdump -s %t.out| FileCheck %s --check-prefix=BEFORE

# BEFORE:      Contents of section .foo:
# BEFORE-NEXT:  201120 11cccccc 2233

# RUN: echo "_foo2 1" > %t_alignment.txt
# RUN: echo "_foo3 4" >> %t_alignment.txt

# RUN: ld.lld --symbol-alignment-file %t_alignment.txt %t.o -o %t2.out
# RUN: llvm-objdump -s %t2.out| FileCheck %s --check-prefix=AFTER

# AFTER:      Contents of section .foo:
# AFTER-NEXT:  201120 1122cccc 33

.section .foo,"ax",@progbits,unique,1
_foo1:
 .byte 0x11

.section .foo,"ax",@progbits,unique,2
.align 4
_foo2:
 .byte 0x22

.section .foo,"ax",@progbits,unique,3
_foo3:
 .byte 0x33
