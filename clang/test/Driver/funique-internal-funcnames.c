// RUN: %clang -### -funique-internal-funcnames %s -c 2>&1 | FileCheck -check-prefix=CHECK-OPT %s
// RUN: %clang -### -funique-internal-funcnames -fno-unique-internal-funcnames %s -c 2>&1 | FileCheck -check-prefix=CHECK-NOOPT %s
// CHECK-OPT: "-funique-internal-funcnames"
// CHECK-NOOPT-NOT: {{-funique-internal-funcnames}}
