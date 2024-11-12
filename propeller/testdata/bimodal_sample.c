// A program which can run in either of two loops and call either of 2 functions
// depending on the input.
volatile int count;

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

__attribute__((noinline)) void compute(double arg){
  if (arg <= 1) {
    for (int i = 0; i < 801; ++i) count += foo(i);
  } else {
    for (int i = 0; i < 401; ++i) count += bar(i);
  }
}

int main(int argc, const char **argv) {
  for (int i = 0; i< 100001; ++i) compute(argc);
}