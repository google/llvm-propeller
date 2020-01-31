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

mapfile -t git_reported_diff_files < <( git diff --numstat ${BASEREV} -- llvm/ compiler-rt/ clang/ lld/ | tr "\t" " " | tr -s " " | cut -f3 -d " " )
for GF in "${git_reported_diff_files[@]}" ; do
    # Try to find GF in ALL_FILES[@]
    FOUND=
    for AF in "${ALL_FILES[@]}" ; do
        if [[ "$GF" == "$AF" ]]; then
            FOUND=1
            break
        fi
    done
    if [[ -z "${FOUND}" ]]; then
        echo "Git reported \"$GF\" is not in \"generate-upstream-patches.filelist\", please update the file."
        exit 1
    fi
done

echo "Generate patches based on: ${BASEINFO}"

if [[ "$(( ${#ALL_FILES[@]} % 2 ))" != "0" ]]; then
    echo "Invalid files lists."
fi

echo "Running clang-format on all files ..."
idx=0
declare -a files_to_format=()
while ((idx<"${#ALL_FILES[@]}")); do
    files_to_format+=( "${ALL_FILES[$((idx+1))]}" )
    idx="$((idx+2))"
done

format_output=`git-clang-format ${BASEREV} -- "${files_to_format[@]}"`
if echo "${format_output}" | grep -qE "^changed files:" ; then
    echo "${format_output}"
    echo Please review by \"git status\" and "(optionally) submit the changes before continue."
    exit 1
else
    echo Great - All files passed clang-format.
fi

idx=0
declare -A component_map
while ((idx<"${#ALL_FILES[@]}")); do
    component="${ALL_FILES[$idx]}"
    file="${ALL_FILES[$((idx+1))]}"
    if [[ "${component}" == "/dev/null" ]]; then
        patch_file=${component}
    else
        patch_file="${DDIR}/upstream-${component}.patch"
        if [[ -z "${component_map[$component]}" ]]; then
            rm -f "${patch_file}"
            component_map[$component]="${patch_file}"
            echo "Creating patch: ${patch_file}"
        fi
        git diff --unified=99999 "${BASEREV}" -- ${file} >> ${patch_file}
    fi
    idx="$((idx+2))"
done

for PF in "${component_map[@]}"; do
    echo "Genearted patch: $PF"
done

popd 1>/dev/null 2>&1
