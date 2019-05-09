// REQUIRES: aarch64
// RUN: llvm-mc -filetype=obj -triple=aarch64-unknown-linux %s -o %t.o
// RUN: ld.lld --hash-style=sysv -shared %t.o -o %t.so
// RUN: llvm-readobj -S -r %t.so | FileCheck %s
// RUN: llvm-objdump -d %t.so | FileCheck --check-prefix=DISASM %s

// CHECK:      Name: .got
// CHECK-NEXT: Type: SHT_PROGBITS
// CHECK-NEXT: Flags [
// CHECK-NEXT:   SHF_ALLOC
// CHECK-NEXT:   SHF_WRITE
// CHECK-NEXT: ]
// CHECK-NEXT: Address: 0x20090
// CHECK-NEXT: Offset:
// CHECK-NEXT: Size: 8
// CHECK-NEXT: Link: 0
// CHECK-NEXT: Info: 0
// CHECK-NEXT: AddressAlignment: 8

// CHECK:      Relocations [
// CHECK-NEXT:   Section ({{.*}}) .rela.dyn {
// CHECK-NEXT:     0x20090 R_AARCH64_GLOB_DAT dat 0x0
// CHECK-NEXT:   }
// CHECK-NEXT: ]

// Page(0x20098) - Page(0x10000) = 0x10000 = 65536
// 0x20098 & 0xff8 = 0x98 = 152

// DISASM: main:
// DISASM-NEXT:   10000:  80 00 00 90   adrp  x0, #65536
// DISASM-NEXT:   10004: 00 48 40 f9   ldr x0, [x0, #144]

.global main,foo,dat
.text
main:
    adrp x0, :got:dat
    ldr x0, [x0, :got_lo12:dat]
.data
dat:
    .word 42
