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