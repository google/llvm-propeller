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

/* same_name_static_functions_2.c */
volatile int never_true = 0; /* Always zero. */

__attribute__((noinline)) int rare(int i) { return i * i; }

/* `work` has internal linkage and shares its name with the function in
 * same_name_static_functions_1.c. It is called in a hot loop. So its entry
 * block is hot. */
static __attribute__((noinline)) int work(int i) {
  if (never_true) /* Never executed. */
    return rare(i);
  if (i % 10 < 4) /* ... in 40% of the calls */
    return i + 1;
  return 0;
}

int run_work_2(int n) {
  volatile int count = 0;
  for (int i = 0; i < n; ++i) count += work(i);
  return count;
}
