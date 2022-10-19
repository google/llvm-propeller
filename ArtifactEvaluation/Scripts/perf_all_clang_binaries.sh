#!/bin/bash

#!/bin/bash

# This assumes that clang_propeller_bolt_binaries has been created
# already. If not, please run optimize_clang.sh!
#
# This script measures performance of baseline, propeller and bolt optimized
# binaries.  It outputs wall time and perf events.

set -eu

# Set the base directory
BASE_DIR="$(pwd)"/clang_propeller_bolt_binaries
if [[ ! -d "${BASE_DIR}" ]]; then
    echo "Directory clang_propeller_bolt_binaries must exist!"
    exit 1
fi


BENCHMARKING_CLANG_BUILD=${BASE_DIR}/benchmarking_clang_build
PATH_TO_TRUNK_LLVM_BUILD=${BASE_DIR}/trunk_llvm_build
CLANG_VERSION=$(sed -Ene 's!^CLANG_EXECUTABLE_VERSION:STRING=(.*)$!\1!p' ${PATH_TO_TRUNK_LLVM_BUILD}/CMakeCache.txt)
PATH_TO_PRISTINE_BASELINE_CLANG_BUILD=${BASE_DIR}/pristine_baseline_build
PATH_TO_OPTIMIZED_PROPELLER_BUILD=${BASE_DIR}/optimized_propeller_build
PATH_TO_INSTRUMENTED_BOLT_CLANG_BUILD=${BASE_DIR}/baseline_bolt_only_clang_build
# Run comparison of baseline verus propeller optimized clang versus bolt
# optimized clang
cd ${BENCHMARKING_CLANG_BUILD}/symlink_to_clang_binary
ln -sf ${PATH_TO_PRISTINE_BASELINE_CLANG_BUILD}/bin/clang-${CLANG_VERSION} clang
ln -sf ${PATH_TO_PRISTINE_BASELINE_CLANG_BUILD}/bin/clang-${CLANG_VERSION} clang++
cd ..
ninja clean
echo "***********  BASELINE ***********"
perf stat -r1 -e instructions,cycles,L1-icache-misses,iTLB-misses -- bash -c "ninja -j48 clang && ninja clean"

cd ${BENCHMARKING_CLANG_BUILD}/symlink_to_clang_binary
ln -sf ${PATH_TO_OPTIMIZED_PROPELLER_BUILD}/bin/clang-${CLANG_VERSION} clang
ln -sf ${PATH_TO_OPTIMIZED_PROPELLER_BUILD}/bin/clang-${CLANG_VERSION} clang++
cd ..
ninja clean
echo "***********  PROPELLER ***********"
perf stat -r1 -e instructions,cycles,L1-icache-misses,iTLB-misses -- bash -c "ninja -j48 clang && ninja clean"

cd ${BENCHMARKING_CLANG_BUILD}/symlink_to_clang_binary
ln -sf ${PATH_TO_INSTRUMENTED_BOLT_CLANG_BUILD}/bin/clang-${CLANG_VERSION}.bolt clang
ln -sf ${PATH_TO_INSTRUMENTED_BOLT_CLANG_BUILD}/bin/clang-${CLANG_VERSION}.bolt clang++
cd ..
ninja clean
echo "*********** BOLT ***********"
perf stat -r1 -e instructions,cycles,L1-icache-misses,iTLB-misses -- bash -c "ninja -j48 clang && ninja clean"
