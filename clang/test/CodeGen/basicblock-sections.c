// REQUIRES: x86-registered-target

// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -S -o - < %s | FileCheck %s --check-prefix=PLAIN
// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -S -fbasicblock-sections=all -fbasicblock-sections=none -o - < %s | FileCheck %s --check-prefix=PLAIN

// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -S -fbasicblock-sections=labels -o - < %s | FileCheck %s --check-prefix=BB_LABELS
// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -S -fbasicblock-sections=all -o - < %s | FileCheck %s --check-prefix=BB_WORLD --check-prefix=BB_ALL
// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -S -fbasicblock-sections=%S/basicblock-sections.funcnames -o - < %s | FileCheck %s --check-prefix=BB_WORLD --check-prefix=BB_LIST
// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -S -fbasicblock-sections=all -funique-bb-section-names -o - < %s | FileCheck %s --check-prefix=UNIQUE

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
// PLAIN: world
//
// BB_LABELS-NOT: section
// BB_LABELS: world
// BB_LABELS-LABEL: a.BB.world
// BB_LABELS-LABEL: aa.BB.world
// BB_LABEL-LABEL: a.BB.another
//
// BB_WORLD: .section .text.world,"ax",@progbits
// BB_WORLD: world
// BB_WORLD: .section .text.world,"ax",@progbits,unique
// BB_WORLD: a.BB.world
// BB_WORLD: .section .text.another,"ax",@progbits
// BB_ALL: .section .text.another,"ax",@progbits,unique
// BB_ALL: a.BB.another
// BB_LIST-NOT: .section .text.another,"ax",@progbits,unique
// BB_LIST: another
// BB_LIST-NOT: a.BB.another
//
// UNIQUE: .section .text.world.a.BB.world
// UNIQUE: .section .text.another.a.BB.another
