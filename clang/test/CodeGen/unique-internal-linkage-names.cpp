// This test checks if internal linkage symbols get unique names with
// -funique-internal-linkage-names option.
// RUN: %clang_cc1 -triple x86_64 -x c++ -S -emit-llvm -o - < %s | FileCheck %s --check-prefix=PLAIN
// RUN: %clang_cc1 -triple x86_64 -x c++ -S -emit-llvm -funique-internal-linkage-names -o - < %s | FileCheck %s --check-prefix=UNIQUE

static int glob;
static int foo() {
  return 0;
}

int (*bar())() {
  return foo;
}

int getGlob() {
  return glob;
}

// Function local static variable and anonymous namespace namespace variable.
namespace {
  int anon_m;
  int getM() {
    return anon_m;
  }
}

int retAnonM() {
  static int fGlob;
  return getM() + fGlob;
}

// Multiversioning symbols
__attribute__((target("default"))) static int mver() {
  return 0;
}

__attribute__((target("sse4.2"))) static int mver() {
  return 1;
}

int mver_call() {
  return mver();
}

// PLAIN: @_ZL4glob = internal global
// PLAIN: @_ZL3foov()
// PLAIN: @_ZN12_GLOBAL__N_14getMEv
// PLAIN: @_ZZ8retAnonMvE5fGlob
// PLAIN: @_ZN12_GLOBAL__N_16anon_mE
// PLAIN: @_ZL4mverv.resolver()
// PLAIN: @_ZL4mverv()
// PLAIN: @_ZL4mverv.sse4.2()
// UNIQUE-NOT: @_ZL4glob = internal global
// UNIQUE-NOT: @_ZL3foov()
// UNIQUE-NOT: @_ZN12_GLOBAL__N_14getMEv
// UNIQUE-NOT: @_ZZ8retAnonMvE5fGlob
// UNIQUE-NOT: @_ZN12_GLOBAL__N_16anon_mE
// UNIQUE-NOT: @_ZL4mverv.resolver()
// UNIQUE-NOT: @_ZL4mverv()
// UNIQUE-NOT: @_ZL4mverv.sse4.2()
// UNIQUE: @_ZL4glob.{{[0-9a-f]+}} = internal global
// UNIQUE: @_ZL3foov.{{[0-9a-f]+}}()
// UNIQUE: @_ZN12_GLOBAL__N_14getMEv.{{[0-9a-f]+}}
// UNIQUE: @_ZZ8retAnonMvE5fGlob.{{[0-9a-f]+}}
// UNIQUE: @_ZN12_GLOBAL__N_16anon_mE.{{[0-9a-f]+}}
// UNIQUE: @_ZL4mverv.{{[0-9a-f]+}}.resolver()
// UNIQUE: @_ZL4mverv.{{[0-9a-f]+}}()
// UNIQUE: @_ZL4mverv.{{[0-9a-f]+}}.sse4.2()
