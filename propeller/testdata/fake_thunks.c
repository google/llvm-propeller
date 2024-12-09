/* fake_thunks.c
 * Executables that contain thunks require branches > 128 MiB, which is too
 * large for the testdata. We use this file to spoof thunks by creating
 * functions that have thunk symbol names. However, as actual functions, they
 * will have `llvm_bb_addr_map` metadata, so they cannot be treated like thunks
 * for all test purposes.
 */
volatile int x = 1;

__attribute__((noinline)) int __AArch64ADRPThunk_test1(int i) { return x + i; }

__attribute__((noinline)) int __AArch64ADRPThunk_test2(int i) {
  return x + i + 1;
}

int main(void) {
  __AArch64ADRPThunk_test1(x);
  __AArch64ADRPThunk_test2(x);

  return 0;
}
