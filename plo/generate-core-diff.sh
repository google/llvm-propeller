#!/bin/bash

MERGE_BASE=$(git merge-base origin/master HEAD)

if [[ -z "${MERGE_BASE}" ]]; then
    echo "Failed to find merge base."
    exit 1
fi

if ! git log "${MERGE_BASE}" 1>/dev/null 2>&1 ; then
    echo "Invalid merge base."
    exit 1
fi

echo "Generating diff against \"$(git log --oneline -1 ${MERGE_BASE})\" ..."

DDIR="$(cd $(dirname $0) && pwd)"
PATCH_FILE=${DDIR}"/plo-dev-diff-on-$(echo ${MERGE_BASE} | cut -c-8)"
pushd ${DDIR}/../ 1>/dev/null 2>&1
git diff ${MERGE_BASE} -- llvm/ clang/ compiler-rt/ lld/ ./create_llvm_prof > ${PATCH_FILE}
popd 1>/dev/null 2>&1

echo "Generated patch file: $PATCH_FILE"
