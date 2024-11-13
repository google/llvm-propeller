static int sample1_func() {
  int sum = 0;
  for (int i = 0; i < (((unsigned long)(void *)(sample1_func)) & 0xfff); ++i)  // NOLINT
    sum += i;
  return sum;
}

int kunfu(int k) {
  return sample1_func() + k;
}

