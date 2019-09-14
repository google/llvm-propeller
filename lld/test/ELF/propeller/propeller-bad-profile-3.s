# REQUIRES: x86
## Test control flow graph is created.

# RUN: llvm-mc -filetype=obj -triple=x86_64-pc-linux %s -o %t.o
# RUN: not ld.lld -propeller=%S/Inputs/bad-propeller-3.data %t.o -o %t.out 2>&1 | FileCheck %s --check-prefix=CHECK

# CHECK: Invalid function index field, at propfile line: 2
