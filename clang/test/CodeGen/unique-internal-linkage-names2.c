// This test checks if -funique-internal-linkage-names uses the right
// path when -ffile-prefix-map= (-fmacro-prefix-map=) is enabled.
// With -fmacro-prefix-map=%p=NEW, this file must be referenced as:
// NEW/unique-internal-linkage-names2.c
// MD5 hash of "NEW/unique-internal-linkage-names2.c" :
// $ echo -n NEW/unique-internal-linkage-names2.c | md5sum
// bd816b262f03c98ffb082cde0847804c
// RUN: %clang_cc1 -triple x86_64 -funique-internal-linkage-names -fmacro-prefix-map=%p=NEW -x c -S -emit-llvm -o - %s | FileCheck %s
static int glob;

int getGlob() {
  return glob;
}

// CHECK: glob.bd816b262f03c98ffb082cde0847804c = internal global
