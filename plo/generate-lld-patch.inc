DDIR=$(cd $(dirname $0) && pwd)

pushd ${DDIR}/../lld 1>/dev/null 2>&1 

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

echo "Generate lld ${LLD_PATCH_NAME} patch based on: ${BASEINFO}"

[[ -e "${DDIR}/lld-${LLD_PATCH_NAME}.patch" ]] && rm -f ${DDIR}/lld-${LLD_PATCH_NAME}.patch
for F in "${THE_FILES[@]}" ; do
    echo "Adding diff for ${F}"
    git diff --unified=99999 "${BASEREV}" -- ${F} >> ${DDIR}/lld-${LLD_PATCH_NAME}.patch
done

echo "Generated ${DDIR}/lld-${LLD_PATCH_NAME}.patch"

popd 1>/dev/null 2>&1
