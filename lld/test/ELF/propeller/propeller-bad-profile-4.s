# REQUIRES: x86
## Test control flow graph is created.

# RUN: llvm-mc -filetype=obj -triple=x86_64 %s -o %t.o
# RUN: not ld.lld -propeller=%S/Inputs/bad-propeller-4.data %t.o -o %t.out 2>&1 | FileCheck %s --check-prefix=CHECK

# CHECK: Function with index number '3' does not exist.
