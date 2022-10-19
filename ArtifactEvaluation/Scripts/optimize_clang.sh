#!/bin/bash

## This script does the following:
## 1. It checks out and builds trunk LLVM.
## 2. It builds multiple clang binaries towards building a
##    propeller and bolt optimized clang binary.
## 3. It runs performance comparisons of a baseline clang
##    binary and the propeller & bolt optimized clang binary.

## Just run optimize_clang.sh
## All artifacts will be created in a directory named
## clang_propeller_bolt_binaries

set -eux

# Set the base directory
CWD="$(pwd)"
BASE_DIR=${CWD}/clang_propeller_bolt_binaries
# If base directory exists, then move it over.
if [[ -d "${BASE_DIR}" ]]; then
    mv ${BASE_DIR} "$(pwd)"/clang_propeller_bolt_binaries.old
    exit 1
fi

mkdir -p "${BASE_DIR}"

# The LLVM sources at git hash 6db71b8f1418170324b49d20f1f7b3f7c5086066 that all builds use
PATH_TO_LLVM_SOURCES=${BASE_DIR}/sources
# The build of LLVM used to build other binaries
PATH_TO_TRUNK_LLVM_BUILD=${BASE_DIR}/trunk_llvm_build
PATH_TO_TRUNK_LLVM_INSTALL=${BASE_DIR}/trunk_llvm_install
# This is used to collect profiles and benchmark the different clang binaries.
# Benchmarking recipe:  Use the clang binary to build clang.
BENCHMARKING_CLANG_BUILD=${BASE_DIR}/benchmarking_clang_build
# The pristine baseline clang binary built with PGO and ThinLTO.
PATH_TO_PRISTINE_BASELINE_CLANG_BUILD=${BASE_DIR}/pristine_baseline_build
# The path to a propeller optimized build of clang.
PATH_TO_OPTIMIZED_PROPELLER_BUILD=${BASE_DIR}/optimized_propeller_build
# Symlink all binaries here
PATH_TO_ALL_BINARIES=${BASE_DIR}/PreBuiltBinaries
# Path to all profiles
PATH_TO_PROFILES=${BASE_DIR}/Profiles
# Results Directory
PATH_TO_ALL_RESULTS=${BASE_DIR}/Results


mkdir -p ${PATH_TO_ALL_RESULTS}
date > ${PATH_TO_ALL_RESULTS}/script_start_time.txt

# Build Trunk LLVM
mkdir -p ${PATH_TO_LLVM_SOURCES} && cd ${PATH_TO_LLVM_SOURCES}
git clone https://github.com/llvm/llvm-project.git
# Set correct git hash here!
cd ${PATH_TO_LLVM_SOURCES}/llvm-project && git reset --hard 6db71b8f1418170324b49d20f1f7b3f7c5086066
mkdir -p ${PATH_TO_TRUNK_LLVM_BUILD} && cd ${PATH_TO_TRUNK_LLVM_BUILD}
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DLLVM_TARGETS_TO_BUILD=X86 -DLLVM_ENABLE_PROJECTS="clang;lld;compiler-rt;bolt" -DCMAKE_C_COMPILER=clang  -DCMAKE_CXX_COMPILER=clang++ -DLLVM_USE_LINKER=lld -DCMAKE_INSTALL_PREFIX="${PATH_TO_TRUNK_LLVM_INSTALL}" -DLLVM_ENABLE_RTTI=On -DLLVM_INCLUDE_TESTS=Off ${PATH_TO_LLVM_SOURCES}/llvm-project/llvm
ninja install
CLANG_VERSION=$(sed -Ene 's!^CLANG_EXECUTABLE_VERSION:STRING=(.*)$!\1!p' ${PATH_TO_TRUNK_LLVM_BUILD}/CMakeCache.txt)
mkdir -p ${PATH_TO_ALL_BINARIES}
cp ${PATH_TO_TRUNK_LLVM_INSTALL}/bin/perf2bolt ${PATH_TO_ALL_BINARIES}
cp ${PATH_TO_TRUNK_LLVM_INSTALL}/bin/llvm-bolt ${PATH_TO_ALL_BINARIES}

# Build FDO/PGO Instrumented binary
PATH_TO_INSTRUMENTED_BINARY=${BASE_DIR}/clang_instrumented_build
INSTRUMENTED_CMAKE_FLAGS=(
  "-DLLVM_OPTIMIZED_TABLEGEN=On"
  "-DCMAKE_BUILD_TYPE=Release"
  "-DLLVM_TARGETS_TO_BUILD=X86"
  "-DLLVM_ENABLE_PROJECTS=clang;lld;compiler-rt"
  "-DCMAKE_C_COMPILER=${PATH_TO_TRUNK_LLVM_INSTALL}/bin/clang"
  "-DCMAKE_CXX_COMPILER=${PATH_TO_TRUNK_LLVM_INSTALL}/bin/clang++"
  "-DLLVM_BUILD_INSTRUMENTED=ON"
  "-DLLVM_USE_LINKER=lld" )

BASELINE_CC_LD_CMAKE_FLAGS=(
  "-DCMAKE_C_FLAGS=-funique-internal-linkage-names"
  "-DCMAKE_CXX_FLAGS=-funique-internal-linkage-names"
  "-DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld -Wl,-gc-sections -Wl,-z,keep-text-section-prefix"
  "-DCMAKE_SHARED_LINKER_FLAGS=-fuse-ld=lld -Wl,-gc-sections -Wl,-z,keep-text-section-prefix"
  "-DCMAKE_MODULE_LINKER_FLAGS=-fuse-ld=lld -Wl,-gc-sections -Wl,-z,keep-text-section-prefix" )

mkdir -p ${PATH_TO_INSTRUMENTED_BINARY} && cd ${PATH_TO_INSTRUMENTED_BINARY}
cmake -G Ninja "${INSTRUMENTED_CMAKE_FLAGS[@]}" "${BASELINE_CC_LD_CMAKE_FLAGS[@]}" "${PATH_TO_LLVM_SOURCES}/llvm-project/llvm"
ninja clang

# Set up benchmarking clang BUILD, used to collect profiles.
mkdir -p ${BENCHMARKING_CLANG_BUILD} && cd ${BENCHMARKING_CLANG_BUILD}
mkdir -p symlink_to_clang_binary && cd symlink_to_clang_binary
ln -sf ${PATH_TO_INSTRUMENTED_BINARY}/bin/clang-${CLANG_VERSION} clang
ln -sf ${PATH_TO_INSTRUMENTED_BINARY}/bin/clang-${CLANG_VERSION} clang++

# Setup cmake for instrumented binary build.  The symlink allows us to replace
# with any clang binary of our choice.
cd ${BENCHMARKING_CLANG_BUILD}
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DLLVM_TARGETS_TO_BUILD=X86 -DLLVM_ENABLE_PROJECTS=clang \
               -DCMAKE_C_COMPILER=${BENCHMARKING_CLANG_BUILD}/symlink_to_clang_binary/clang \
               -DCMAKE_CXX_COMPILER=${BENCHMARKING_CLANG_BUILD}/symlink_to_clang_binary/clang++ \
              ${PATH_TO_LLVM_SOURCES}/llvm-project/llvm
ninja -t commands | head -100 >& ./instr_commands.sh
chmod +x ./instr_commands.sh
./instr_commands.sh

# Convert PGO instrumented profiles to profdata
cd ${PATH_TO_INSTRUMENTED_BINARY}/profiles
${PATH_TO_TRUNK_LLVM_BUILD}/bin/llvm-profdata merge -output=clang.profdata *
# Copy the instrumented profile for later use to repro the build.
mkdir -p ${PATH_TO_PROFILES}
cp ${PATH_TO_INSTRUMENTED_BINARY}/profiles/clang.profdata ${PATH_TO_PROFILES}

# Common CMAKE Flags
# #"-DCMAKE_BUILD_TYPE=Release"
# Enable ThinLTO too here.
COMMON_CMAKE_FLAGS=(
  "-DLLVM_OPTIMIZED_TABLEGEN=On"
  "-DCMAKE_BUILD_TYPE=Release"
  "-DLLVM_TARGETS_TO_BUILD=X86"
  "-DLLVM_ENABLE_PROJECTS=clang"
  "-DCMAKE_C_COMPILER=${PATH_TO_TRUNK_LLVM_INSTALL}/bin/clang"
  "-DCMAKE_CXX_COMPILER=${PATH_TO_TRUNK_LLVM_INSTALL}/bin/clang++"
  "-DLLVM_USE_LINKER=lld"
  "-DLLVM_ENABLE_LTO=Thin"
  "-DLLVM_PROFDATA_FILE=${PATH_TO_INSTRUMENTED_BINARY}/profiles/clang.profdata" )

PRISTINE_CC_LD_CMAKE_FLAGS=(
  "-DCMAKE_C_FLAGS=-funique-internal-linkage-names"
  "-DCMAKE_CXX_FLAGS=-funique-internal-linkage-names"
  "-DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld"
  "-DCMAKE_SHARED_LINKER_FLAGS=-fuse-ld=lld"
  "-DCMAKE_MODULE_LINKER_FLAGS=-fuse-ld=lld" )

# Build Pristine Baseline first.
mkdir -p ${PATH_TO_PRISTINE_BASELINE_CLANG_BUILD} && cd ${PATH_TO_PRISTINE_BASELINE_CLANG_BUILD}
cmake -G Ninja "${COMMON_CMAKE_FLAGS[@]}" "${PRISTINE_CC_LD_CMAKE_FLAGS[@]}" ${PATH_TO_LLVM_SOURCES}/llvm-project/llvm
ninja clang
cp bin/clang ${PATH_TO_ALL_BINARIES}/clang.baseline

# Additional Flags to build an instrumented BOLT binary. BOLT needs -Wl,-q which
# dumps all static relocations for later rewriting.
INSTRUMENTED_BOLT_CC_LD_CMAKE_FLAGS=(
  "-DCMAKE_C_FLAGS=-funique-internal-linkage-names"
  "-DCMAKE_CXX_FLAGS=-funique-internal-linkage-names"
  "-DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld -Wl,-q"
  "-DCMAKE_SHARED_LINKER_FLAGS=-fuse-ld=lld -Wl,-q"
  "-DCMAKE_MODULE_LINKER_FLAGS=-fuse-ld=lld -Wl,-q" )

# Build Baseline BOLT Only Instrumented Clang Binary
PATH_TO_INSTRUMENTED_BOLT_CLANG_BUILD=${BASE_DIR}/baseline_bolt_only_clang_build
mkdir -p ${PATH_TO_INSTRUMENTED_BOLT_CLANG_BUILD} && cd ${PATH_TO_INSTRUMENTED_BOLT_CLANG_BUILD}
cmake -G Ninja "${COMMON_CMAKE_FLAGS[@]}" "${INSTRUMENTED_BOLT_CC_LD_CMAKE_FLAGS[@]}" ${PATH_TO_LLVM_SOURCES}/llvm-project/llvm
ninja clang

# Additional Flags to build an Instrumented Propeller binary.
INSTRUMENTED_PROPELLER_CC_LD_CMAKE_FLAGS=(
  "-DCMAKE_C_FLAGS=-funique-internal-linkage-names -fbasic-block-sections=labels"
  "-DCMAKE_CXX_FLAGS=-funique-internal-linkage-names -fbasic-block-sections=labels"
  "-DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld  -Wl,--lto-basic-block-sections=labels"
  "-DCMAKE_SHARED_LINKER_FLAGS=-fuse-ld=lld -Wl,--lto-basic-block-sections=labels"
  "-DCMAKE_MODULE_LINKER_FLAGS=-fuse-ld=lld -Wl,--lto-basic-block-sections=labels" )

# Build Propeller Instrumented Clang Binary.
PATH_TO_INSTRUMENTED_PROPELLER_CLANG_BUILD=${BASE_DIR}/baseline_propeller_only_clang_build
mkdir -p ${PATH_TO_INSTRUMENTED_PROPELLER_CLANG_BUILD} && cd ${PATH_TO_INSTRUMENTED_PROPELLER_CLANG_BUILD}
cmake -G Ninja "${COMMON_CMAKE_FLAGS[@]}" "${INSTRUMENTED_PROPELLER_CC_LD_CMAKE_FLAGS[@]}" ${PATH_TO_LLVM_SOURCES}/llvm-project/llvm
ninja clang

# Set up Benchmarking and BUILD.
# We want to use one profile to optimize both BOLT and Propeller for
# consistency.
# Use the Pristine Baseline build to collect profiles.
cd ${BENCHMARKING_CLANG_BUILD}/symlink_to_clang_binary
ln -sf ${PATH_TO_PRISTINE_BASELINE_CLANG_BUILD}/bin/clang-${CLANG_VERSION} clang
ln -sf ${PATH_TO_PRISTINE_BASELINE_CLANG_BUILD}/bin/clang-${CLANG_VERSION} clang++
cd ${BENCHMARKING_CLANG_BUILD}
ninja clean

# Profile labels binary, just 100 compilations should be good enough.
ninja -t commands | head -100 >& ./perf_commands.sh
chmod +x ./perf_commands.sh
perf record -e cycles:u -j any,u -- ./perf_commands.sh
ls perf.data

# Copy the profiles for future use
cd ${BENCHMARKING_CLANG_BUILD}
cp perf.data ${PATH_TO_PROFILES}

# Convert Profiles to BOLT format.
PATH_TO_PERF2BOLT=${PATH_TO_TRUNK_LLVM_INSTALL}/bin/perf2bolt
cd ${PATH_TO_PROFILES}
/usr/bin/time -v ${PATH_TO_PERF2BOLT} ${PATH_TO_INSTRUMENTED_BOLT_CLANG_BUILD}/bin/clang-${CLANG_VERSION} -p ${PATH_TO_PROFILES}/perf.data -o ${PATH_TO_PROFILES}/perf.fdata -w ${PATH_TO_PROFILES}/perf.yaml 2> ${PATH_TO_ALL_RESULTS}/mem_bolt_profile_conversion.txt

# Rewrite binary to get a BOLT optimized clang.
PATH_TO_LLVMBOLT=${PATH_TO_TRUNK_LLVM_INSTALL}/bin/llvm-bolt
cd ${PATH_TO_PROFILES}
/usr/bin/time -v ${PATH_TO_LLVMBOLT} ${PATH_TO_INSTRUMENTED_BOLT_CLANG_BUILD}/bin/clang-${CLANG_VERSION} -o ${PATH_TO_PROFILES}/clang-${CLANG_VERSION}.bolt -b ${PATH_TO_PROFILES}/perf.yaml -reorder-blocks=ext-tsp -reorder-functions=hfsort+ -split-functions -split-all-cold -dyno-stats -icf=1 -use-gnu-stack -inline-small-functions -simplify-rodata-loads -plt=hot 2> ${PATH_TO_ALL_RESULTS}/mem_bolt_rewrite.txt
# Copy over the bolted clang binary
cp ${PATH_TO_PROFILES}/clang-${CLANG_VERSION}.bolt ${PATH_TO_INSTRUMENTED_BOLT_CLANG_BUILD}/bin
# Save a copy of the bolted clang binary
mv ${PATH_TO_PROFILES}/clang-${CLANG_VERSION}.bolt ${PATH_TO_ALL_BINARIES}/clang.bolt

# Clone and Build create_llvm_prof, the tool to convert to propeller format.
PATH_TO_CREATE_LLVM_PROF=${BASE_DIR}/create_llvm_prof_build
mkdir -p ${PATH_TO_CREATE_LLVM_PROF} && cd ${PATH_TO_CREATE_LLVM_PROF}

git clone --recursive https://github.com/google/autofdo.git
cd autofdo && git fetch && git checkout origin/2022.09.merge
cd ${PATH_TO_CREATE_LLVM_PROF}
mkdir -p bin && cd bin
cmake -G Ninja -DCMAKE_INSTALL_PREFIX="." \
      -DCMAKE_C_COMPILER="${PATH_TO_TRUNK_LLVM_INSTALL}/bin/clang" \
      -DCMAKE_CXX_COMPILER="${PATH_TO_TRUNK_LLVM_INSTALL}/bin/clang++" \
      -DLLVM_PATH="${PATH_TO_TRUNK_LLVM_INSTALL}" ../autofdo/
ninja
ls create_llvm_prof

cp create_llvm_prof ${PATH_TO_ALL_BINARIES}

/usr/bin/time -v ${PATH_TO_CREATE_LLVM_PROF}/bin/create_llvm_prof  --format=propeller --binary=${PATH_TO_INSTRUMENTED_PROPELLER_CLANG_BUILD}/bin/clang-${CLANG_VERSION}  --profile=${PATH_TO_PROFILES}/perf.data --out=${PATH_TO_PROFILES}/cluster.txt  --propeller_symorder=${PATH_TO_PROFILES}/symorder.txt --profiled_binary_name=clang-${CLANG_VERSION} 2> ${PATH_TO_ALL_RESULTS}/mem_propeller_profile_conversion.txt

# Build a Propeller Optimized binary.
OPTIMIZED_PROPELLER_CC_LD_CMAKE_FLAGS=(
  "-DCMAKE_C_FLAGS=-funique-internal-linkage-names -fbasic-block-sections=list=${PATH_TO_PROFILES}/cluster.txt"
  "-DCMAKE_CXX_FLAGS=-funique-internal-linkage-names -fbasic-block-sections=list=${PATH_TO_PROFILES}/cluster.txt"
  "-DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld -Wl,--lto-basic-block-sections=${PATH_TO_PROFILES}/cluster.txt -Wl,--symbol-ordering-file=${PATH_TO_PROFILES}/symorder.txt -Wl,--no-warn-symbol-ordering -Wl,-gc-sections -Wl,-z,keep-text-section-prefix"
  "-DCMAKE_SHARED_LINKER_FLAGS=-fuse-ld=lld -Wl,--lto-basic-block-sections=${PATH_TO_PROFILES}/cluster.txt -Wl,--symbol-ordering-file=${PATH_TO_PROFILES}/symorder.txt -Wl,--no-warn-symbol-ordering -Wl,-gc-sections -Wl,-z,keep-text-section-prefix"
  "-DCMAKE_MODULE_LINKER_FLAGS=-fuse-ld=lld -Wl,--lto-basic-block-sections=${PATH_TO_PROFILES}/cluster.txt -Wl,--symbol-ordering-file=${PATH_TO_PROFILES}/symorder.txt -Wl,--no-warn-symbol-ordering -Wl,-gc-sections -Wl,-z,keep-text-section-prefix" )

mkdir -p ${PATH_TO_OPTIMIZED_PROPELLER_BUILD} && cd ${PATH_TO_OPTIMIZED_PROPELLER_BUILD}
cmake -G Ninja "${COMMON_CMAKE_FLAGS[@]}" "${OPTIMIZED_PROPELLER_CC_LD_CMAKE_FLAGS[@]}" ${PATH_TO_LLVM_SOURCES}/llvm-project/llvm
ninja clang
cp bin/clang ${PATH_TO_ALL_BINARIES}/clang.propeller
# Measure the peak RSS of the final link action on cached native object files.
rm bin/clang-16 && /usr/bin/time -v ninja clang 2> ${PATH_TO_ALL_RESULTS}/mem_propeller_build.txt

# Run comparison of baseline verus propeller optimized clang versus bolt
# optimized clang
cd ${BENCHMARKING_CLANG_BUILD}/symlink_to_clang_binary
ln -sf ${PATH_TO_PRISTINE_BASELINE_CLANG_BUILD}/bin/clang-${CLANG_VERSION} clang
ln -sf ${PATH_TO_PRISTINE_BASELINE_CLANG_BUILD}/bin/clang-${CLANG_VERSION} clang++
cd ..
ninja clean
perf stat -r1 -e instructions,cycles,L1-icache-misses,iTLB-misses -- bash -c "ninja -j48 clang && ninja clean" 2> ${PATH_TO_ALL_RESULTS}/perf_clang_baseline.txt

cd ${BENCHMARKING_CLANG_BUILD}/symlink_to_clang_binary
ln -sf ${PATH_TO_OPTIMIZED_PROPELLER_BUILD}/bin/clang-${CLANG_VERSION} clang
ln -sf ${PATH_TO_OPTIMIZED_PROPELLER_BUILD}/bin/clang-${CLANG_VERSION} clang++
cd ..
ninja clean
perf stat -r1 -e instructions,cycles,L1-icache-misses,iTLB-misses -- bash -c "ninja -j48 clang && ninja clean" 2> ${PATH_TO_ALL_RESULTS}/perf_clang_propeller.txt

cd ${BENCHMARKING_CLANG_BUILD}/symlink_to_clang_binary
ln -sf ${PATH_TO_INSTRUMENTED_BOLT_CLANG_BUILD}/bin/clang-${CLANG_VERSION}.bolt clang
ln -sf ${PATH_TO_INSTRUMENTED_BOLT_CLANG_BUILD}/bin/clang-${CLANG_VERSION}.bolt clang++
cd ..
ninja clean
perf stat -r1 -e instructions,cycles,L1-icache-misses,iTLB-misses -- bash -c "ninja -j48 clang && ninja clean" 2> ${PATH_TO_ALL_RESULTS}/perf_clang_bolt.txt

cd ${CWD} && ln -sf clang_propeller_bolt_binaries/Results Results

date > ${PATH_TO_ALL_RESULTS}/script_end_time.txt
