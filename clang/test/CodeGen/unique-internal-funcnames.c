// RUN: %clang_cc1 -triple x86_64 -S -emit-llvm -o - < %s | FileCheck %s --check-prefix=PLAIN
// RUN: %clang_cc1 -triple x86_64 -S -emit-llvm -funique-internal-funcnames -o - < %s | FileCheck %s --check-prefix=UNIQUE

static int foo() {
  return 0;
}

int (*bar())() {
  return foo;
}

// PLAIN: @foo()
// UNIQUE-NOT: @foo()
// UNIQUE: @foo.{{[0-9a-f]+}}()
