// Copyright 2024 The Propeller Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/* thunks.c
 * Executables that contain thunks require branches > 128 MiB. We force the
 * linker to generate thunks by adding a large fill section after .text using a
 * linker script (thunks.lds) and placing functions in `.text.hot` to ensure
 * that they are in a different section from `main()`.
 *
 * Apart from the `section` attributes, this file is identical to `sample.c`.
 */

volatile int count;

static int goose() { return 13; }  // NOLINT

__attribute__((noinline, section(".text.hot"))) double this_is_very_code(
    double tt) {
  volatile double dead = 3434343434,
                  beef = 56565656; /* Avoid compiler optimizing away */
  return dead / beef + beef / dead + tt / 183;
}

__attribute__((noinline, section(".text.hot"))) int compute_flag(int i) {
  if (i % 10 < 4) /* ... in 40% of the iterations */
    return i + 1;
  return 0;
}

int sample1_func() { return 13; }

__attribute__((section(".text"))) int main() {
  int i;
  int flag;
  volatile double x = 1212121212,
                  y = 121212; /* Avoid compiler optimizing away */

  for (i = 0; i < 800000000; i++) {
    flag = compute_flag(i);

    /* Some other code */
    count++;

    if (flag)
      x += x / y + y / x; /* Execute expensive division if flag is set */
    if (count % 137949234 == 183) {
      x += this_is_very_code(count) + sample1_func();
    }
  }
  return 0;
}