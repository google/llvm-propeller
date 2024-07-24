#!/bin/bash

set -ue -o pipefail

declare AEDIR=
AEDIR="$(cd "$(dirname "$0")"; cd ..; pwd)"
declare -r DEMODIR="${AEDIR}"/DemonstratePropellerBuildCaching
declare -r BDIR="$DEMODIR"
declare -r TRUNK_LLVM_INSTALL_DIR="${AEDIR}/clang_propeller_bolt_binaries/trunk_llvm_install"
declare -r LLVM_SOURCE="${AEDIR}/clang_propeller_bolt_binaries/sources/llvm-project"

if [[ ! -e "${AEDIR}/clang_propeller_bolt_binaries/Profiles/cluster.txt" ]] || \
     [[ ! -e "${AEDIR}/clang_propeller_bolt_binaries/Profiles/symorder.txt" ]]; then
  echo "Required profiles not present, please run ${AEDIR}/Scripts/optimize_clang.sh first."
  exit 1
fi

function config_and_build() {
  mkdir -p "$DEMODIR"
  local -r fdo_profile="${AEDIR}/clang_propeller_bolt_binaries/clang_instrumented_build/profiles/clang.profdata"
  local -r link_flags="-fuse-ld=lld -Wl,-gc-sections -Wl,-z,keep-text-section-prefix"
  local -a cmake_args=( "-DLLVM_OPTIMIZED_TABLEGEN=On"
			"-DCMAKE_BUILD_TYPE=Release"
			"-DLLVM_TARGETS_TO_BUILD=X86"
			"-DLLVM_ENABLE_PROJECTS=clang"
			"-DCMAKE_C_COMPILER=${TRUNK_LLVM_INSTALL_DIR}/bin/clang"
			"-DCMAKE_CXX_COMPILER=${TRUNK_LLVM_INSTALL_DIR}/bin/clang++"
			"-DCMAKE_C_FLAGS=-funique-internal-linkage-names"
			"-DCMAKE_CXX_FLAGS=-funique-internal-linkage-names"
                        "-DCMAKE_EXE_LINKER_FLAGS=$link_flags"
                        "-DCMAKE_SHARED_LINKER_FLAGS=$link_flags"
                        "-DCMAKE_MODULE_LINKER_FLAGS=$link_flags"
			"-DLLVM_USE_LINKER=lld"
			"-DLLVM_ENABLE_LTO=Thin"
			"-DLLVM_PROFDATA_FILE=$fdo_profile" )
    cd $DEMODIR
    cmake -G Ninja "${cmake_args[@]}" "${LLVM_SOURCE}/llvm"
    ninja clang
}

rm -fr "${DEMODIR}"
config_and_build

########################
cd "$BDIR"

clang_link_cmd="$(ninja -t commands clang | grep -Ee ' -o bin/clang-16 ' | sed -nEe 's/^.*\s+&&\s+(.*)\s+&&\s+(.*)$/\1/p')"
clang_executable="$(echo $clang_link_cmd | sed -nEe 's!^([^ ]+/clang\+\+) .*$!\1!p')"
if [[ ! -x $clang_executable ]]; then echo "Compiler is not clang: $clang_executable" ; fi
echo "clang=${clang_executable}"
lld_cmd="$($clang_link_cmd -v 2>&1 | grep -F "ld.lld\" ")"
lld_executable="$(echo $lld_cmd | sed -nEe 's/"([^ ]+\/ld.lld)" .*$/\1/p')"
if [[ ! -x $lld_executable ]]; then echo "Linker is not lld: $lld_executable" ; fi
echo "ld.lld=${lld_executable}"

mapfile -t lld_args < <(echo $lld_cmd | sed -nEe 's/^.*ld.lld" (.*)$/\1/p' | tr ' ' '\n')

# 0. extract libXXXXX.a to lib-extracted/
declare thin_link_args=()  # thin_link_args replace replace .a inputs with its .o contents.
mkdir -p "${BDIR}/lib-extracted"
for a in "${lld_args[@]}"; do
  if echo $a | grep -qE "^lib/.*\.a"; then
    T="$(basename $a)"
    subdir="${T%%.a}"
    cd "${BDIR}/lib-extracted"
    mkdir -p $subdir
    cd $subdir
    ar x ${BDIR}/"$a"
    mapfile -t archive_contents < <(ar t "${BDIR}/$a")
    thin_link_args+=( "--start-lib" )
    for ac in "${archive_contents[@]}"; do
      thin_link_args+=( "lib-extracted/$subdir/$ac" )
    done
    thin_link_args+=( "--end-lib" )
  else
    thin_link_args+=( "$a" )
  fi
done

cd "$BDIR"
# 1. generating thinlto-index files "*.thinlto.bc"
export IFS=$'\n'
echo "${thin_link_args[*]}" > ${BDIR}/thin_link_args
unset IFS
echo "--thinlto-index-only" >> ${BDIR}/thin_link_args
cat <<EOF > ${BDIR}/thin_link.sh
#!/bin/bash

"$lld_executable" "@${BDIR}/thin_link_args"

EOF
chmod +x ${BDIR}/thin_link.sh
echo "Generated ${BDIR}/thin_link.sh"

# 2. generate thinlto backend compiler commands
backend_compile_command_file="${BDIR}/backend_compile_commands"
rm -f "$backend_compile_command_file"
declare -a backend_link_args=( )
cd "$BDIR"
for line in "${thin_link_args[@]}" ; do
  if echo $line | grep -qE '\.o$' ; then
    if file $line 2>&1 | grep -qF "LLVM IR bitcode" ; then
      mkdir -p "$(dirname "${BDIR}/native/$line")"
      echo echo \"Compiling $line "-\\>" native/$line\" ";" ${clang_executable} -x ir -c $line -fthinlto-index=$line.thinlto.bc -o ${BDIR}/native/$line >> "$backend_compile_command_file"
      backend_link_args+=( "${BDIR}/native/$line" )
      continue
    fi
  fi
  backend_link_args+=( "$line" )
done

cat <<EOF > "${BDIR}/backend_compile.sh"
#!/bin/bash

xargs -a "${backend_compile_command_file}" -L1 -I{} "-P$(cat /proc/cpuinfo  | grep -E "processor\s+:\s+[0-9]+" | wc -l)" -- bash -c "{}"

EOF

chmod +x "${BDIR}/backend_compile.sh"
echo Generated "${BDIR}/backend_compile.sh"

export IFS=$'\n'
echo "${backend_link_args[*]}" > ${BDIR}/backend_link_args
unset IFS

cat <<EOF > ${BDIR}/backend_link.sh
#!/bin/bash

"$lld_executable" @"${BDIR}/backend_link_args"

EOF

chmod +x ${BDIR}/backend_link.sh

echo Generated "$BDIR/backend_link.sh"

echo "Running thin-linking ..."
${BDIR}/thin_link.sh

echo "Running backend compiling and linking ..."
/usr/bin/time -v bash -c "{ ${BDIR}/backend_compile.sh ; ${BDIR}/backend_link.sh ; }"
