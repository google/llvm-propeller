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

/* sample.c */
volatile int count;

static int goose() { return 13; }  // NOLINT

__attribute__((noinline)) double this_is_very_code(double tt) {
  volatile double dead = 3434343434,
                  beaf = 56565656; /* Avoid compiler optimizing away */
  return dead / beaf + beaf / dead + tt / 183;
}

__attribute__((noinline)) int compute_flag(int i) {
  if (i % 10 < 4) /* ... in 40% of the iterations */
    return i + 1;
  return 0;
}

__attribute__((section(".anycall.anysection"))) __attribute__((noinline)) int
anycall() {
  if (count % 13 == 0) return 12;
  return 13;
}

__attribute__((section(".othercall.othersection")))
__attribute__((noinline)) int
othercall() {
  if (count % 1234567891 == 0) {
    count += 1;
    return 12;
  }
  return 13;
}

__attribute__((section(".text.unlikely"))) __attribute__((noinline)) int
unlikelycall() {
  return 13;
}

__attribute__((noinline)) int sample1_func() { return 13; }

int main(void) {
  int i;
  int flag;
  volatile double x = 1212121212,
                  y = 121212; /* Avoid compiler optimizing away */

  for (i = 0; i < 800000000; i++) {
    flag = compute_flag(i) + anycall() + othercall();

    /* Some other code */
    count++;

    if (flag)
      x += x / y + y / x; /* Execute expensive division if flag is set */
    if (count % 137949234 == 183) {
      x += this_is_very_code(count) + sample1_func() + unlikelycall();
    }
  }
  return 0;
}
