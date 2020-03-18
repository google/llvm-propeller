// REQUIRES: x86-registered-target

// RUN: %clang -target x86_64 -S -o - %s | FileCheck %s --check-prefix=PLAIN
// RUN: %clang -target x86_64 -S -funique-internal-funcnames -o -  %s | FileCheck %s --check-prefix=UNIQUE

static int foo() {
  return 0;
}

int (*bar())() {
  return foo;
}

// PLAIN: foo:
// UNIQUE-NOT: foo:
// UNIQUE: foo.{{[0-9a-f]+}}:
