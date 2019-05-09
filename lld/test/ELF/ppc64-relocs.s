# REQUIRES: ppc

# RUN: llvm-mc -filetype=obj -triple=powerpc64le-unknown-linux %s -o %t.o
# RUN: ld.lld --no-toc-optimize %t.o -o %t
# RUN: llvm-readelf -x .rodata -x .eh_frame %t | FileCheck %s --check-prefix=DATALE
# RUN: llvm-objdump -d --no-show-raw-insn %t | FileCheck %s

# RUN: llvm-mc -filetype=obj -triple=powerpc64-unknown-linux %s -o %t.o
# RUN: ld.lld --no-toc-optimize %t.o -o %t
# RUN: llvm-readelf -x .rodata -x .eh_frame %t | FileCheck %s --check-prefix=DATABE
# RUN: llvm-objdump -d --no-show-raw-insn %t | FileCheck %s

.text
.global _start
_start:
.Lfoo:
	li      0,1
	li      3,42
	sc

.section        .rodata,"a",@progbits
        .p2align        2
.LJTI0_0:
        .long   .LBB0_2-.LJTI0_0

.section        .toc,"aw",@progbits
.L1:
.quad           22, 37, 89, 47
.LC0:
        .tc .LJTI0_0[TC],.LJTI0_0

.section .R_PPC64_TOC16_LO_DS,"ax",@progbits
.globl .FR_PPC64_TOC16_LO_DS
.FR_PPC64_TOC16_LO_DS:
  ld 1, .L1@toc@l(2)

# CHECK: Disassembly of section .R_PPC64_TOC16_LO_DS:
# CHECK-EMPTY:
# CHECK: .FR_PPC64_TOC16_LO_DS:
# CHECK: 1001000c:       ld 1, -32768(2)

.section .R_PPC64_TOC16_LO,"ax",@progbits
.globl .FR_PPC64_TOC16_LO
.FR_PPC64_TOC16_LO:
  addi  1, 2, .L1@toc@l

# CHECK: Disassembly of section .R_PPC64_TOC16_LO:
# CHECK-EMPTY:
# CHECK: .FR_PPC64_TOC16_LO:
# CHECK: 10010010:       addi 1, 2, -32768

.section .R_PPC64_TOC16_HI,"ax",@progbits
.globl .FR_PPC64_TOC16_HI
.FR_PPC64_TOC16_HI:
  addis 1, 2, .L1@toc@h

# CHECK: Disassembly of section .R_PPC64_TOC16_HI:
# CHECK-EMPTY:
# CHECK: .FR_PPC64_TOC16_HI:
# CHECK: 10010014:       addis 1, 2, -1

.section .R_PPC64_TOC16_HA,"ax",@progbits
.globl .FR_PPC64_TOC16_HA
.FR_PPC64_TOC16_HA:
  addis 1, 2, .L1@toc@ha

# CHECK: Disassembly of section .R_PPC64_TOC16_HA:
# CHECK-EMPTY:
# CHECK: .FR_PPC64_TOC16_HA:
# CHECK: 10010018:       addis 1, 2, 0

.section .R_PPC64_REL24,"ax",@progbits
.globl .FR_PPC64_REL24
.FR_PPC64_REL24:
  b .Lfoox
.section .R_PPC64_REL24_2,"ax",@progbits
.Lfoox:

# CHECK: Disassembly of section .R_PPC64_REL24:
# CHECK-EMPTY:
# CHECK: .FR_PPC64_REL24:
# CHECK: 1001001c:       b .+4

.section .R_PPC64_REL14,"ax",@progbits
.globl .FR_PPC64_REL14
.FR_PPC64_REL14:
  beq .Lfooy
.section .R_PPC64_REL14_2,"ax",@progbits
.Lfooy:

# CHECK: Disassembly of section .R_PPC64_REL14:
# CHECK-EMPTY:
# CHECK: .FR_PPC64_REL14:
# CHECK: 10010020:       bt 2, .+4

.section .R_PPC64_ADDR16_LO,"ax",@progbits
.globl .FR_PPC64_ADDR16_LO
.FR_PPC64_ADDR16_LO:
  li 1, .Lfoo@l

# CHECK: Disassembly of section .R_PPC64_ADDR16_LO:
# CHECK-EMPTY:
# CHECK: .FR_PPC64_ADDR16_LO:
# CHECK: 10010024:       li 1, 0

.section .R_PPC64_ADDR16_HI,"ax",@progbits
.globl .FR_PPC64_ADDR16_HI
.FR_PPC64_ADDR16_HI:
  li 1, .Lfoo@h

# CHECK: Disassembly of section .R_PPC64_ADDR16_HI:
# CHECK-EMPTY:
# CHECK: .FR_PPC64_ADDR16_HI:
# CHECK: 10010028:       li 1, 4097

.section .R_PPC64_ADDR16_HA,"ax",@progbits
.globl .FR_PPC64_ADDR16_HA
.FR_PPC64_ADDR16_HA:
  li 1, .Lfoo@ha

# CHECK: Disassembly of section .R_PPC64_ADDR16_HA:
# CHECK-EMPTY:
# CHECK: .FR_PPC64_ADDR16_HA:
# CHECK: 1001002c:       li 1, 4097

.section .R_PPC64_ADDR16_HIGHER,"ax",@progbits
.globl .FR_PPC64_ADDR16_HIGHER
.FR_PPC64_ADDR16_HIGHER:
  li 1, .Lfoo@higher

# CHECK: Disassembly of section .R_PPC64_ADDR16_HIGHER:
# CHECK-EMPTY:
# CHECK: .FR_PPC64_ADDR16_HIGHER:
# CHECK: 10010030:       li 1, 0

.section .R_PPC64_ADDR16_HIGHERA,"ax",@progbits
.globl .FR_PPC64_ADDR16_HIGHERA
.FR_PPC64_ADDR16_HIGHERA:
  li 1, .Lfoo@highera

# CHECK: Disassembly of section .R_PPC64_ADDR16_HIGHERA:
# CHECK-EMPTY:
# CHECK: .FR_PPC64_ADDR16_HIGHERA:
# CHECK: 10010034:       li 1, 0

.section .R_PPC64_ADDR16_HIGHEST,"ax",@progbits
.globl .FR_PPC64_ADDR16_HIGHEST
.FR_PPC64_ADDR16_HIGHEST:
  li 1, .Lfoo@highest

# CHECK: Disassembly of section .R_PPC64_ADDR16_HIGHEST:
# CHECK-EMPTY:
# CHECK: .FR_PPC64_ADDR16_HIGHEST:
# CHECK: 10010038:       li 1, 0

.section .R_PPC64_ADDR16_HIGHESTA,"ax",@progbits
.globl .FR_PPC64_ADDR16_HIGHESTA
.FR_PPC64_ADDR16_HIGHESTA:
  li 1, .Lfoo@highesta

# CHECK: Disassembly of section .R_PPC64_ADDR16_HIGHESTA:
# CHECK-EMPTY:
# CHECK: .FR_PPC64_ADDR16_HIGHESTA:
# CHECK: 1001003c:       li 1, 0

.section  .R_PPC64_REL32, "ax",@progbits
.globl .FR_PPC64_REL32
.FR_PPC64_REL32:
  addis 5, 2, .LC0@toc@ha
  ld 5, .LC0@toc@l(5)
.LBB0_2:
  add 3, 3, 4

# DATALE: '.rodata':
# DATALE: 0x100001c8 80fe0000

# DATABE: '.rodata':
# DATABE: 0x100001c8 0000fe80

# Address of rodata + value stored at rodata entry
# should equal address of LBB0_2.
# 0x10000190 + 0xfeb4 = 0x10010044
# CHECK: Disassembly of section .R_PPC64_REL32:
# CHECK-EMPTY:
# CHECK: .FR_PPC64_REL32:
# CHECK: 10010040:       addis 5, 2, 0
# CHECK: 10010044:       ld 5, -32736(5)
# CHECK: 10010048:       add 3, 3, 4

.section .R_PPC64_REL64, "ax",@progbits
.globl  .FR_PPC64_REL64
.FR_PPC64_REL64:
        .cfi_startproc
        .cfi_personality 148, __foo
        li 0, 1
        li 3, 55
        sc
        .cfi_endproc
__foo:
  li 3,0

# Check that the personality (relocated by R_PPC64_REL64) in the .eh_frame
# equals the address of __foo.
# 0x100001e2 + 0x76fe = 0x10010058
# DATALE: section '.eh_frame':
# DATALE: 0x100001e0 {{....}}76fe

# DATABE: section '.eh_frame':
# DATABE: 0x100001e0 {{[0-9a-f]+ [0-9a-f]+}} fe76{{....}}

# CHECK: __foo
# CHECK-NEXT: 10010058:       li 3, 0
