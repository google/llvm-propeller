// link with clang -fuse-ld=lld -o libro_sample.so -fpic -shared -O2 ro.c -Wl,-build-id

#include <stdio.h>

const char apple[] = "lasjfoaur03u4jrklfnzfkofjdklzngiow3ehjtdfn";

int foo() {
  int n, i, sum = 0;
  for (n = 1; n < 10000; ++n) {
    for (i = n % 11; i <= 20; ++i) {
      sum += i + (int)(apple[i]);
    }
  }
  return i;
}
