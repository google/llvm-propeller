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

// link with clang -fuse-ld=lld -o libro_sample.so -fpic -shared -O2 ro.c
// -Wl,-build-id

#include <stdio.h>

const char apple[] = "lasjfoaur03u4jrklfnzfkofjdklzngiow3ehjtdfn";

int foo() {
  int n, i, sum = 0;
  for (n = 1; n < 10000; ++n) {
    for (i = n % 11; i <= 20; ++i) {
      sum += i + (int)(apple[i]);
    }
  }
  return i;
}
