// RUN: %clang -### -funique-bb-section-names %s -S 2>&1 | FileCheck -check-prefix=CHECK-OPT %s
// RUN: %clang -### -funique-bb-section-names -fno-unique-bb-section-names %s -S 2>&1 | FileCheck -check-prefix=CHECK-NOOPT %s
// CHECK-OPT: "-funique-bb-section-names"
// CHECK-NOOPT-NOT: "-funique-bb-section-names"
