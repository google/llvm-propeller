#!/bin/bash

# This assumes that clang_propeller_bolt_binaries has been created
# already. If not, please run optimize_clang.sh!
#
# This script measures sizes of various clang binaries.

set -eu

# Set the base directory
BASE_DIR="$(pwd)"/clang_propeller_bolt_binaries
if [[ ! -d "${BASE_DIR}" ]]; then
    echo "Directory clang_propeller_bolt_binaries must exist!"
    exit 1
fi

PATH_TO_TRUNK_LLVM_BUILD=${BASE_DIR}/trunk_llvm_build
PATH_TO_TRUNK_LLVM_INSTALL=${BASE_DIR}/trunk_llvm_install
CLANG_VERSION=$(sed -Ene 's!^CLANG_EXECUTABLE_VERSION:STRING=(.*)$!\1!p' ${PATH_TO_TRUNK_LLVM_BUILD}/CMakeCache.txt)
PATH_TO_PRISTINE_BASELINE_CLANG_BUILD=${BASE_DIR}/pristine_baseline_build
PATH_TO_INSTRUMENTED_BOLT_CLANG_BUILD=${BASE_DIR}/baseline_bolt_only_clang_build
PATH_TO_INSTRUMENTED_PROPELLER_CLANG_BUILD=${BASE_DIR}/baseline_propeller_only_clang_build
PATH_TO_OPTIMIZED_PROPELLER_BUILD=${BASE_DIR}/optimized_propeller_build

printf "Baseline Stats\n" > ${BASE_DIR}/Results/sizes_clang.txt
printf "Total Size\n" >> ${BASE_DIR}/Results/sizes_clang.txt
ls -l ${PATH_TO_PRISTINE_BASELINE_CLANG_BUILD}/bin/clang-${CLANG_VERSION} | awk '{print $5}'  >> ${BASE_DIR}/Results/sizes_clang.txt
printf ".text .ehframe bbaddrmap relocs\n" >> ${BASE_DIR}/Results/sizes_clang.txt
${PATH_TO_TRUNK_LLVM_INSTALL}/bin/llvm-readelf -S ${PATH_TO_PRISTINE_BASELINE_CLANG_BUILD}/bin/clang-${CLANG_VERSION} | awk '{ if ($2 == ".text") { text = strtonum("0x" $6); } if ($2 == ".eh_frame") { eh_frame = strtonum("0x" $6); } if ($2 == ".llvm_bbaddrmap") { bbaddrmap = strtonum("0x" $6); } if ($2 == ".relocs") { relocs = strtonum("0x" $6); } }  END { printf "%d %d %d %d\n", text, eh_frame, bbaddrmap, relocs; }'>> ${BASE_DIR}/Results/sizes_clang.txt

printf "\nPropeller Instrumented Stats (PM)\n" >> ${BASE_DIR}/Results/sizes_clang.txt
printf "Total Size\n"  >> ${BASE_DIR}/Results/sizes_clang.txt
ls -l ${PATH_TO_INSTRUMENTED_PROPELLER_CLANG_BUILD}/bin/clang-${CLANG_VERSION} |  awk '{print $5}'  >> ${BASE_DIR}/Results/sizes_clang.txt
printf ".text .ehframe bbaddrmap relocs\n"  >> ${BASE_DIR}/Results/sizes_clang.txt
${PATH_TO_TRUNK_LLVM_INSTALL}/bin/llvm-readelf -S ${PATH_TO_INSTRUMENTED_PROPELLER_CLANG_BUILD}/bin/clang-${CLANG_VERSION} | awk '{ if ($2 == ".text") { text = strtonum("0x" $6); } if ($2 == ".eh_frame") { eh_frame = strtonum("0x" $6); } if ($2 == ".llvm_bb_addr_map") { bbaddrmap = strtonum("0x" $6); } if ($2 == ".relocs") { relocs = strtonum("0x" $6); } }  END { printf "%d %d %d %d\n", text, eh_frame, bbaddrmap, relocs; }'  >> ${BASE_DIR}/Results/sizes_clang.txt

printf "\nPropeller Optimized Stats (PO)\n" >> ${BASE_DIR}/Results/sizes_clang.txt
printf "Total Size\n" >> ${BASE_DIR}/Results/sizes_clang.txt
ls -l ${PATH_TO_OPTIMIZED_PROPELLER_BUILD}/bin/clang-${CLANG_VERSION} |  awk '{print $5}' >> ${BASE_DIR}/Results/sizes_clang.txt
printf ".text .ehframe bbaddrmap relocs\n" >> ${BASE_DIR}/Results/sizes_clang.txt
${PATH_TO_TRUNK_LLVM_INSTALL}/bin/llvm-readelf -S ${PATH_TO_OPTIMIZED_PROPELLER_BUILD}/bin/clang-${CLANG_VERSION} | awk '{ if ($2 == ".text") { text = strtonum("0x" $6); } if ($2 == ".eh_frame") { eh_frame = strtonum("0x" $6); } if ($2 == ".llvm_bb_addr_map") { bbaddrmap = strtonum("0x" $6); } if ($2 == ".relocs") { relocs = strtonum("0x" $6); } }  END { printf "%d %d %d %d\n", text, eh_frame, bbaddrmap, relocs; }' >> ${BASE_DIR}/Results/sizes_clang.txt

printf "\nBOLT Instrumented Stats (BM)\n" >> ${BASE_DIR}/Results/sizes_clang.txt
printf "Total Size\n" >> ${BASE_DIR}/Results/sizes_clang.txt
ls -l ${PATH_TO_INSTRUMENTED_BOLT_CLANG_BUILD}/bin/clang-${CLANG_VERSION} |  awk '{print $5}' >> ${BASE_DIR}/Results/sizes_clang.txt
printf ".text .ehframe bbaddrmap relocs\n" >> ${BASE_DIR}/Results/sizes_clang.txt
${PATH_TO_TRUNK_LLVM_INSTALL}/bin/llvm-readelf -S ${PATH_TO_INSTRUMENTED_BOLT_CLANG_BUILD}/bin/clang-${CLANG_VERSION} | awk '{ if ($2 == ".text") { text = strtonum("0x" $6); } if ($2 == ".eh_frame") { eh_frame = strtonum("0x" $6); } if ($2 == ".llvm_bb_addr_map") { bbaddrmap = strtonum("0x" $6); } if ($2 == ".rela.text") { relocs = strtonum("0x" $6); } }  END { printf "%d %d %d %d\n", text, eh_frame, bbaddrmap, relocs; }' >> ${BASE_DIR}/Results/sizes_clang.txt

printf "\nBOLT Optimized Stats (BO)\n" >> ${BASE_DIR}/Results/sizes_clang.txt
printf "Total Size\n" >> ${BASE_DIR}/Results/sizes_clang.txt
ls -l ${PATH_TO_INSTRUMENTED_BOLT_CLANG_BUILD}/bin/clang-${CLANG_VERSION}.bolt |  awk '{print $5}' >> ${BASE_DIR}/Results/sizes_clang.txt
printf ".text .ehframe bbaddrmap relocs\n" >> ${BASE_DIR}/Results/sizes_clang.txt
${PATH_TO_TRUNK_LLVM_INSTALL}/bin/llvm-readelf -S ${PATH_TO_INSTRUMENTED_BOLT_CLANG_BUILD}/bin/clang-${CLANG_VERSION}.bolt | awk '{ if ($2 == ".text") { text = strtonum("0x" $6); } if ($2 == ".eh_frame") { eh_frame = strtonum("0x" $6); } if ($2 == ".llvm_bb_addr_map") { bbaddrmap = strtonum("0x" $6); } if ($2 == ".rela.text") { relocs = strtonum("0x" $6); } }  END { printf "%d %d %d %d\n", text, eh_frame, bbaddrmap, relocs; }' >> ${BASE_DIR}/Results/sizes_clang.txt
