#!/bin/bash

DDIR=$(cd $(dirname $0) && pwd)

pushd ${DDIR}/../lld 1>/dev/null 2>&1 

# Total 28 files.
declare -a DD=( ELF/Propeller.h
		ELF/Propeller.cpp
		ELF/PropellerELFCfg.h
		ELF/PropellerELFCfg.cpp
		include/lld/Common/PropellerCommon.h
		test/ELF/propeller/propeller-bad-profile-1.s
		test/ELF/propeller/propeller-bad-profile-2.s
		test/ELF/propeller/propeller-bad-profile-3.s
		test/ELF/propeller/propeller-bad-profile-4.s
		test/ELF/propeller/propeller-bad-profile-5.s
		test/ELF/propeller/propeller-bbsections-dump.s
		test/ELF/propeller/propeller-compressed-strtab-lto.s
		test/ELF/propeller/propeller-compressed-strtab.s
		test/ELF/propeller/propeller-error-on-bblabels.s
		test/ELF/propeller/propeller-keep-bb-symbols.s
		test/ELF/propeller/propeller-lto-bbsections-dump.s
		test/ELF/propeller/propeller-opt-all-combinations.s
		test/ELF/propeller/propeller-skip.s
		test/ELF/propeller/propeller-symbol-order-dump.s
		test/ELF/propeller/Inputs/bad-propeller-1.data
		test/ELF/propeller/Inputs/bad-propeller-2.data
		test/ELF/propeller/Inputs/bad-propeller-3.data
		test/ELF/propeller/Inputs/bad-propeller-4.data
		test/ELF/propeller/Inputs/bad-propeller-5.data
		test/ELF/propeller/Inputs/propeller-2.data
		test/ELF/propeller/Inputs/propeller-3.data
		test/ELF/propeller/Inputs/propeller.data
		test/ELF/propeller/Inputs/sample.c )

BASEREV=`git log --oneline --parents --merges -n 88 | grep "Merge branch 'master' into plo-dev." | head -n 1 | cut -d" " -f3`

if [[ -z "${BASEREV}" ]]; then
    echo "Failed to find base revision for plo-dev branch."
    exit 1
fi

[[ -e "${DDIR}/lld-propeller.patch" ]] && rm -f ${DDIR}/lld-propeller.patch
for F in "${DD[@]}" ; do
    git diff "${BASEREV}" -- ${F} >> ${DDIR}/lld-propeller.patch
done

echo "Generated ${DDIR}/lld-propeller.patch"

popd 1>/dev/null 2>&1
