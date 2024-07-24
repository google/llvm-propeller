#!/bin/bash

# This assumes that clang_propeller_bolt_binaries has been created
# already. If not, please run optimize_clang.sh!
#
# This script measures peak RSS to convert raw profiles to Propeller format.

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
CLANG_VERSION=$(sed -Ene 's!^CLANG_EXECUTABLE_VERSION:STRING=(.*)$!\1!p' ${PATH_TO_TRUNK_LLVM_BUILD}/CMakeCache.txt)
PATH_TO_CREATE_LLVM_PROF=${BASE_DIR}/create_llvm_prof_build
PATH_TO_INSTRUMENTED_PROPELLER_CLANG_BUILD=${BASE_DIR}/baseline_propeller_only_clang_build

/usr/bin/time -v ${PATH_TO_CREATE_LLVM_PROF}/bin/create_llvm_prof  --format=propeller --binary=${PATH_TO_INSTRUMENTED_PROPELLER_CLANG_BUILD}/bin/clang-${CLANG_VERSION}  --profile=${PATH_TO_PROFILES}/perf.data --out=${PATH_TO_PROFILES}/cluster.txt  --propeller_symorder=${PATH_TO_PROFILES}/symorder.txt --profiled_binary_name=clang-${CLANG_VERSION}
