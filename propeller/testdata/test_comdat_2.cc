#include "test_comdat.h"  // NOLINT

#include <stdio.h>

int goo();

int foo2() {
  return 18;
}

int main() {
  Foo foo;
  fprintf(stderr, "Hello %d\n", foo.do_work() + goo() + foo2());
  return 0;
}
