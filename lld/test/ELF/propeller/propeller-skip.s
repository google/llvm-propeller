# REQUIRES: x86
## Test lld honors "@" directive. The input propeller-3.data contains a
## "@" with an invalid output file name. ld.lld won't activate propeller
## because "-o" output name does not match what is following "@".

# RUN: llvm-mc -filetype=obj -triple=x86_64 %s -o %t.o
# RUN: ld.lld -propeller=%S/Inputs/propeller-3.data %t.o -o %t.out 2>&1 | FileCheck %s --check-prefix=CHECK

# CHECK: warning: [Propeller]: Propeller skipped
