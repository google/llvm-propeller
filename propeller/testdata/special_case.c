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

__attribute__((noinline)) int foo(int arg) {
  if (arg < 0) {
    arg *= arg;
    if (arg < 3) goto E;
    goto L;
  }
  // Two empty basic blocks.
  asm("");
L:
  asm("");
E:
  return arg;
}

int main(int argc, const char** argv) {
  int value = argc - 5;
  if (value < 5) {
    // A call followed by nop, and then the next basic block.
    foo(value / 2);
  }
  return value;
}