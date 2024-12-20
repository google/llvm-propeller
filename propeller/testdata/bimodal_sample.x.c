#include <stdlib.h>

// A program which can run in either of two loops (or both) and call either of
// 2 functions (or both) depending on the input.
volatile int count;
volatile int sum;

__attribute__((noinline)) double foo(double v) {
  volatile double dead = 3434343434,
                  beaf = 56565656; /* Avoid compiler optimizing away */
  return dead / beaf + beaf / dead + v / 183;
}

__attribute__((noinline)) double bar(double v) {
  volatile double dead = 1212121212,
                  beaf = 34343434; /* Avoid compiler optimizing away */
  return dead * v / beaf + beaf / dead + v / 187;
}

__attribute__((noinline)) int compute(double arg) {
  if (arg == 1 || arg >= 3) {
    for (int i = 0; i < arg * 4; ++i) count += foo(i);
  } else if (arg == 2) {
    for (int i = 0; i < arg * 4; ++i) count += bar(i);
  }
  return count;
}

int main(int argc, const char **argv) {
  for (int i = 0; i < 10000001; ++i) {
    sum += compute(argc);
    if (argc == 3) sum += compute(argc - 1);
  }
}