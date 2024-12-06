/* fake_thunks.c */
volatile int x = 1;

__attribute__((noinline)) int __AArch64ADRPThunk_test1(int i) {
  return x + i;
}

__attribute__((noinline)) int __AArch64ADRPThunk_test2(int i) {
  return x + i + 1;
}

int sample1_func() { return 13; }

int main(void) {
  __AArch64ADRPThunk_test1(x);
  __AArch64ADRPThunk_test2(x);

  return 0;
}
