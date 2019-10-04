DDIR=$(cd $(dirname $0) && pwd)

pushd ${DDIR}/../lld 1>/dev/null 2>&1 

BASEREV=`git log --oneline --parents --merges -n 88 | grep -E "Merge .*branch '.*master' into plo-dev." | head -n 1 | cut -d" " -f3`

if [[ -z "${BASEREV}" ]]; then
    echo "Failed to find base revision for plo-dev branch."
    exit 1
fi

BASESVN=$(git log -1 ${BASEREV} | grep -F "llvm-svn: " | sed -nEe 's/^.*llvm-svn: (.*)/\1/p')

if [[ -z "${BASESVN}" ]]; then
    echo "Failed to find base revision for plo-dev branch."
    exit 1
fi

echo "Generate lld ${LLD_PATCH_NAME} patch based on ${BASEREV}/r${BASESVN}."

[[ -e "${DDIR}/lld-bbsections.patch" ]] && rm -f ${DDIR}/lld-${LLD_PATCH_NAME}.patch
for F in "${THE_FILES[@]}" ; do
    git diff "${BASEREV}" -- ${F} >> ${DDIR}/lld-${LLD_PATCH_NAME}.patch
done

echo "Generated ${DDIR}/lld-${LLD_PATCH_NAME}.patch"

popd 1>/dev/null 2>&1
