// Check that -fpropeller flag invokes the correct options.
// RUN: %clang -### %s -target x86_64-unknown-linux -fpropeller-label -flto=thin 2>&1 | FileCheck %s -check-prefix=CHECK_PROPELLER_LABEL
// RUN: %clang -### %s -target x86_64-unknown-linux -fpropeller-optimize=perf.propeller -flto=thin 2>&1 | FileCheck %s -check-prefix=CHECK_PROPELLER_OPT

// CHECK_PROPELLER_LABEL: "-fbasic-block-sections=labels"
// CHECK_PROPELLER_LABEL: "-funique-internal-linkage-names"
// CHECK_PROPELLER_LABEL: "--lto-basicblock-sections=labels"
//
// CHECK_PROPELLER_OPT: "-fbasic-block-sections=perf.propeller"
// CHECK_PROPELLER_OPT: "-funique-internal-linkage-names"
// CHECK_PROPELLER_OPT: "--propeller=perf.propeller"
// CHECK_PROPELLER_OPT: "--lto-basicblock-sections=perf.propeller"
// CHECK_PROPELLER_OPT: "--optimize-bb-jumps"
