#!/bin/bash

# This assumes that clang_propeller_bolt_binaries has been created
# already. If not, please run optimize_clang.sh!
#
# This script measures peak RSS to convert raw profiles to BOLT format.

set -eu

# Set the base directory
BASE_DIR="$(pwd)"/clang_propeller_bolt_binaries
if [[ ! -d "${BASE_DIR}" ]]; then
    echo "Directory clang_propeller_bolt_binaries must exist!"
    exit 1
fi

PATH_TO_PROFILES=${BASE_DIR}/Profiles
PATH_TO_TRUNK_LLVM_BUILD=${BASE_DIR}/trunk_llvm_build
PATH_TO_TRUNK_LLVM_INSTALL=${BASE_DIR}/trunk_llvm_install
PATH_TO_PERF2BOLT=${PATH_TO_TRUNK_LLVM_INSTALL}/bin/perf2bolt
PATH_TO_INSTRUMENTED_BOLT_CLANG_BUILD=${BASE_DIR}/baseline_bolt_only_clang_build
CLANG_VERSION=$(sed -Ene 's!^CLANG_EXECUTABLE_VERSION:STRING=(.*)$!\1!p' ${PATH_TO_TRUNK_LLVM_BUILD}/CMakeCache.txt)

cd ${PATH_TO_PROFILES}
/usr/bin/time -v ${PATH_TO_PERF2BOLT} ${PATH_TO_INSTRUMENTED_BOLT_CLANG_BUILD}/bin/clang-${CLANG_VERSION} -p ${PATH_TO_PROFILES}/perf.data -o ${PATH_TO_PROFILES}/perf.fdata -w ${PATH_TO_PROFILES}/perf.yaml
