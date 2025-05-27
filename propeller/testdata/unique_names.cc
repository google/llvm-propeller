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

// This file is used to generate propeller_unique_names.out which
// contains duplicate uniq-named functions. See BUILD rule
// ":duplicate_unique_names".

static int foo() { return 20; }

#ifdef VER1
int goo() { return foo(); }
#elif defined VER2
int goo2() { return foo() + 5; }
#endif

#ifdef MAIN
int goo();
int goo2();
int main(int argc, char* argv[]) {
  return goo() + goo2() + reinterpret_cast<unsigned long>(foo);  // NOLINT
}
#endif
