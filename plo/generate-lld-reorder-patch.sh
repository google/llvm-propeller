#!/bin/bash

DDIR=$(cd $(dirname $0) && pwd)

pushd ${DDIR}/../lld 1>/dev/null 2>&1 

# Total 7 files.
declare -a DD=( ELF/PropellerBBReordering.h
                ELF/PropellerBBReordering.cpp
                ELF/PropellerFuncOrdering.h
                ELF/PropellerFuncOrdering.cpp
                test/ELF/propeller/propeller-layout-function-ordering.s
                test/ELF/propeller/propeller-layout-function-with-loop.s
                test/ELF/propeller/propeller-layout-optimal-fallthrough.s )


BASEREV=`git log --oneline --parents --merges -n 88 | grep "Merge branch 'master' into plo-dev." | head -n 1 | cut -d" " -f3`

if [[ -z "${BASEREV}" ]]; then
    echo "Failed to find base revision for plo-dev branch."
    exit 1
fi

[[ -e "${DDIR}/lld-reorder.patch" ]] && rm -f ${DDIR}/lld-reorder.patch
for F in "${DD[@]}" ; do
    git diff "${BASEREV}" -- ${F} >> ${DDIR}/lld-reorder.patch
done

echo "Generated ${DDIR}/lld-reorder.patch"

popd 1>/dev/null 2>&1
