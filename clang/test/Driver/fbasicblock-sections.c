// RUN: %clang -### -fbasicblock-sections=none %s -S 2>&1 | FileCheck -check-prefix=CHECK-OPT-NONE %s
// RUN: %clang -### -fbasicblock-sections=all %s -S 2>&1 | FileCheck -check-prefix=CHECK-OPT-ALL %s
// RUN: %clang -### -fbasicblock-sections=list=%s %s -S 2>&1 | FileCheck -check-prefix=CHECK-OPT-LIST %s
// RUN: %clang -### -fbasicblock-sections=labels %s -S 2>&1 | FileCheck -check-prefix=CHECK-OPT-LABELS %s
//
// CHECK-OPT-NONE: "-fbasicblock-sections=none"
// CHECK-OPT-ALL: "-fbasicblock-sections=all"
// CHECK-OPT-LIST: "-fbasicblock-sections={{[^ ]*}}fbasicblock-sections.c"
// CHECK-OPT-LABELS: "-fbasicblock-sections=labels"
