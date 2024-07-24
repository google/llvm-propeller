#!/bin/bash

declare AEDIR=
AEDIR="$(cd "$(dirname "$0")"; cd ..; pwd)"
declare -r DEMODIR="${AEDIR}"/DemonstratePropellerBuildCaching
declare -r BDIR="$DEMODIR"

if [[ ! -d "$BDIR" ]]; then
  echo "$BDIR does not exist, please run ${AEDIR}/Scripts/cache_all_artifacts.sh first."
  exit 1
fi

function abs_path() {
  echo "$(cd "$(dirname "$1")"; pwd)"/"$(basename "$1")"
}

cd "${BDIR}"

clang_link_cmd="$(ninja -t commands clang | grep -Ee ' -o bin/clang-16 ' | sed -nEe 's/^.*\s+&&\s+(.*)\s+&&\s+(.*)$/\1/p')"
clang_executable="$(echo $clang_link_cmd | sed -nEe 's!^([^ ]+/clang\+\+) .*$!\1!p')"
if [[ ! -x $clang_executable ]]; then echo "Compiler is not clang: $clang_executable" ; fi

mapfile -t clang_args < <(echo $clang_link_cmd | tr ' ' '\n')

declare -r basic_block_sections="${AEDIR}/clang_propeller_bolt_binaries/Profiles/cluster.txt"
declare -r symbol_ordering_file="${AEDIR}/clang_propeller_bolt_binaries/Profiles/symorder.txt"

echo "Clang: $clang_executable"
echo "Basic block sections: $basic_block_sections"
echo "Symbol ordering file: $symbol_ordering_file"

find native -name "*.o" -exec bash -c "readelf -Ws {} | grep -E '\s+FUNC\s+(LOCAL|WEAK|GLOBAL)' | sed -nEe 's,^.*\s+([^ ]+)$,\1 {},p'" \; > file_func

LC_COLLATE=c sort --key=1 file_func > file_func.sorted
LC_COLLATE=c sort --key=1 "$symbol_ordering_file" > symbol_ordering.sorted

LC_COLLATE=c join -j 1 file_func.sorted symbol_ordering.sorted | cut -f2 -d" " | sort -u > recompile_file_list

mapfile -t recompile_files < <(cat recompile_file_list)

rm -f recompile_propeller_commands
for f in "${recompile_files[@]}"; do
  ff="${f#native/}"
  echo echo Propeller optimizing "$(basename $f)" "... ;" "${clang_executable}" -x ir -c "$ff" -fbasic-block-sections=list="$(abs_path "${basic_block_sections}")" -fthinlto-index=${ff}.thinlto.bc -o "$f" >> recompile_propeller_commands
done

cat <<EOF > ${BDIR}/recompile_propeller.sh
#!/bin/bash

cd "$BDIR"
xargs -a recompile_propeller_commands "-P$(cat /proc/cpuinfo  | grep -E "processor\s+:\s+[0-9]+" | wc -l)" -I{} -- bash -c "{}"

EOF

chmod +x "${BDIR}/recompile_propeller.sh"

cat <<EOF > "${BDIR}/generate_propeller_binary.sh"
#!/bin/bash

cd "$BDIR"
./recompile_propeller.sh
./backend_link.sh
EOF

chmod +x "${BDIR}/generate_propeller_binary.sh"
echo Generated: ${BDIR}/generate_propeller_binary.sh
