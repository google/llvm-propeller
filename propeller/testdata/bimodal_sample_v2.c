// Copyright 2025 The Propeller Authors.
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

// A program which can run in either of two loops and call either of 2 functions
// depending on the input. If the input has zero or one argument, it calls
// `foo`. Otherwise, it calls `bar`.
volatile int count;
volatile int feedback;

__attribute__((noinline)) double foo(double v) {
  volatile double dead = 3434343434,
                  beaf = 56565656; /* Avoid compiler optimizing away */
  feedback = 0;
  return dead / beaf + beaf / dead + v / 183;
}

__attribute__((noinline)) double bar(double v) {
  volatile double dead = 1212121212,
                  beaf = 34343434; /* Avoid compiler optimizing away */
  feedback = 1;
  return dead * v / beaf + beaf / dead + v / 187;
}

__attribute__((noinline)) void compute(double arg) {
  if (arg <= 2) {
    for (int i = 0; i < 801; ++i) count += foo(i);
  } else {
    for (int i = 0; i < 401; ++i) count += bar(i);
  }
  if (feedback == 0) {
    count += foo(0);
  } else {
    count += bar(0);
  }
}

int main(int argc, const char** argv) {
  for (int i = 0; i < 1000001; ++i) compute(argc);
}