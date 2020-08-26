// REQUIRES: x86-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -S -o - < %s | FileCheck %s --check-prefix=PLAIN
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -S -fbasic-block-sections=all -fbasic-block-sections=none -o - < %s | FileCheck %s --check-prefix=PLAIN

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -S -fbasic-block-sections=labels -o - < %s | FileCheck %s --check-prefix=BB_LABELS
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -S -fbasic-block-sections=all -o - < %s | FileCheck %s --check-prefix=BB_WORLD --check-prefix=BB_ALL
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -S -fbasic-block-sections=list=%S/Inputs/basic-block-sections.funcnames -o - < %s | FileCheck %s --check-prefix=BB_WORLD --check-prefix=BB_LIST
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -S -fbasic-block-sections=all -funique-basic-block-section-names -o - < %s | FileCheck %s --check-prefix=UNIQUE

int world(int a) {
  if (a > 10)
    return 10;
  else if (a > 5)
    return 5;
  else
    return 0;
}

int another(int a) {
  if (a > 10)
    return 20;
  return 0;
}

// PLAIN-NOT: section
// PLAIN: world:
//
// BB_LABELS-NOT: section
// BB_LABELS: world:
// BB_LABELS: .Lfunc_begin0:
// BB_LABELS: .LBB_END0_0:
// BB_LABELS: .LBB0_1:
// BB_LABELS: .LBB_END0_1:
// BB_LABELS: .LBB0_3:
// BB_LABELS: .LBB_END0_3:
// BB_LABELS: .LBB0_4:
// BB_LABELS: .LBB_END0_4:
// BB_LABELS: .LBB0_5:
// BB_LABELS: .LBB_END0_5:
// BB_LABELS: .Lfunc_end0:
//
// BB_LABELS:       .section  .bb_info,"o",@progbits,.text
// BB_LABELS-NEXT:  .quad  .Lfunc_begin0
// BB_LABELS-NEXT:  .byte  6
// BB_LABELS-NEXT:  .uleb128 .Lfunc_begin0-.Lfunc_begin0
// BB_LABELS-NEXT:  .uleb128 .LBB_END0_0-.Lfunc_begin0
// BB_LABELS-NEXT:  .byte  0
// BB_LABELS-NEXT:  .uleb128 .LBB0_1-.Lfunc_begin0
// BB_LABELS-NEXT:  .uleb128 .LBB_END0_1-.LBB0_1
// BB_LABELS-NEXT:  .byte  0
// BB_LABELS-NEXT:  .uleb128 .LBB0_2-.Lfunc_begin0
// BB_LABELS-NEXT:  .uleb128 .LBB_END0_2-.LBB0_2
// BB_LABELS-NEXT:  .byte  0
// BB_LABELS-NEXT:  .uleb128 .LBB0_3-.Lfunc_begin0
// BB_LABELS-NEXT:  .uleb128 .LBB_END0_3-.LBB0_3
// BB_LABELS-NEXT:  .byte  0
// BB_LABELS-NEXT:  .uleb128 .LBB0_4-.Lfunc_begin0
// BB_LABELS-NEXT:  .uleb128 .LBB_END0_4-.LBB0_4
// BB_LABELS-NEXT:  .byte  0
// BB_LABELS-NEXT:  .uleb128 .LBB0_5-.Lfunc_begin0
// BB_LABELS-NEXT:  .uleb128 .LBB_END0_5-.LBB0_5
// BB_LABELS-NEXT:  .byte  1
//
// BB_LABELS: another:
// BB_LABELS: .Lfunc_begin1:
// BB_LABELS: .LBB_END1_0:
// BB_LABELS: .LBB1_1:
// BB_LABELS: .LBB_END1_1:
// BB_LABELS: .LBB1_2:
// BB_LABELS: .LBB_END1_2:
// BB_LABELS: .LBB1_3:
// BB_LABELS: .LBB_END1_3:
// BB_LABELS: .Lfunc_end1:
//
// BB_LABELS:       .section  .bb_info,"o",@progbits,.text
// BB_LABELS-NEXT:  .quad  .Lfunc_begin1
// BB_LABELS-NEXT:  .byte  4
// BB_LABELS-NEXT:  .uleb128 .Lfunc_begin1-.Lfunc_begin1
// BB_LABELS-NEXT:  .uleb128 .LBB_END1_0-.Lfunc_begin1
// BB_LABELS-NEXT:  .byte  0
// BB_LABELS-NEXT:  .uleb128 .LBB1_1-.Lfunc_begin1
// BB_LABELS-NEXT:  .uleb128 .LBB_END1_1-.LBB1_1
// BB_LABELS-NEXT:  .byte  0
// BB_LABELS-NEXT:  .uleb128 .LBB1_2-.Lfunc_begin1
// BB_LABELS-NEXT:  .uleb128 .LBB_END1_2-.LBB1_2
// BB_LABELS-NEXT:  .byte  0
// BB_LABELS-NEXT:  .uleb128 .LBB1_3-.Lfunc_begin1
// BB_LABELS-NEXT:  .uleb128 .LBB_END1_3-.LBB1_3
// BB_LABELS-NEXT:  .byte  1
//
// BB_WORLD: .section .text.world,"ax",@progbits{{$}}
// BB_WORLD: world:
// BB_WORLD: .section .text.world,"ax",@progbits,unique
// BB_WORLD: world.1:
// BB_WORLD: .section .text.another,"ax",@progbits
// BB_ALL: .section .text.another,"ax",@progbits,unique
// BB_ALL: another.1:
// BB_LIST-NOT: .section .text.another,"ax",@progbits,unique
// BB_LIST: another:
// BB_LIST-NOT: another.1:
//
// UNIQUE: .section .text.world.world.1,
// UNIQUE: .section .text.another.another.1,
