// Copyright 2026 The Propeller Authors.
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

/* same_name_static_functions_1.c */
volatile int sum1;

/* `work` has internal linkage and shares its name with the function in
 * same_name_static_functions_2.c. It is called only once and runs a hot loop.
 * So its entry block is cold. */
static __attribute__((noinline)) int work(int n) {
  for (int i = 0; i < n; ++i) {
    if (i % 3 == 0) /* ... in a third of the iterations */
      sum1 += i;
    else
      sum1 -= 1;
  }
  return sum1;
}

int run_work_1(int n) { return work(n); }
