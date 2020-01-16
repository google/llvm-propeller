// REQUIRES: x86-registered-target

// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -S -o - < %s | FileCheck %s --check-prefix=PLAIN
// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -S -funique-internal-funcnames -fno-unique-internal-funcnames -o - < %s | FileCheck %s --check-prefix=PLAIN
// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -S -funique-internal-funcnames -o - < %s | FileCheck %s --check-prefix=UNIQUE

static int foo() {
  return 0;
}

int (*bar())() {
  return foo;
}

// PLAIN: foo:
// UNIQUE-NOT: foo:
// UNIQUE: foo.$
