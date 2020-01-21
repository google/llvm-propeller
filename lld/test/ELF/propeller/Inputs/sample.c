/* sample.c */
volatile int count;

__attribute__((noinline)) int compute_flag(int i) {
  if (i % 10 < 4) /* ... in 40% of the iterations */
    return i + 1;
  return 0;
}

int main(void) {
  int i;
  int flag;
  volatile double x = 1212121212,
                  y = 121212; /* Avoid compiler optimizing away */

  for (i = 0; i < 2000000000; i++) {
    flag = compute_flag(i);

    /* Some other code */
    count++;

    if (flag)
      x += x / y + y / x; /* Execute expensive division if flag is set */
  }
  return 0;
}
