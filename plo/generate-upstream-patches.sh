DDIR=$(cd $(dirname $0) && pwd)

source ${DDIR}/generate-upstream-patches.filelist

pushd ${DDIR}/.. 1>/dev/null 2>&1 

BASEREV=$(git merge-base origin/master HEAD | cut -c-8)

if [[ -z "${BASEREV}" ]]; then
    echo "Failed to find base revision for plo-dev branch."
    exit 1
fi

BASEINFO=$(git log -1 --oneline ${BASEREV})

if [[ -z "${BASEINFO}" ]]; then
    echo "Failed to find base revision for plo-dev branch."
    exit 1
fi

echo "Generate patches based on: ${BASEINFO}"

idx=0
declare -A component_map

while ((idx<"${#ALL_FILES[@]}")); do
    component="${ALL_FILES[$idx]}"
    file="${ALL_FILES[$((idx+1))]}"
    patch_file="${DDIR}/upstream-${component}.patch"
    if [[ -z "${component_map[$component]}" ]]; then
        rm -f "${patch_file}"
        component_map[$component]="${patch_file}"
        echo "Creating patch: ${patch_file}"
    fi
    git diff --unified=99999 "${BASEREV}" -- ${file} >> ${patch_file}
    idx="$((idx+2))"
done

for PF in "${component_map[@]}"; do
    echo "Genearted patch: $PF"
done

popd 1>/dev/null 2>&1
