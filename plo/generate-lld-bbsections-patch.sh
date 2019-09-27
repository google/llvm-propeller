#!/bin/bash

DDIR=$(cd $(dirname $0) && pwd)

pushd ${DDIR}/../lld 1>/dev/null 2>&1 

# Total 20 files.
declare -a DD=( Common/Args.cpp
                ELF/Arch/X86_64.cpp
                ELF/CMakeLists.txt
                ELF/Config.h
                ELF/Driver.cpp
                ELF/InputSection.h
                ELF/InputSection.cpp
                ELF/LTO.cpp
                ELF/Options.td
                ELF/OutputSections.cpp
                ELF/Relocations.h
                ELF/SyntheticSections.h
                ELF/SyntheticSections.cpp
                ELF/Target.h
                ELF/Writer.cpp
                include/lld/Common/Args.h
                test/ELF/lto/linker-script-symbols-ipo.ll
                test/ELF/lto/wrap-2.ll
                test/ELF/wrap-plt.s
                test/ELF/x86-64-plt.s )

BASEREV=`git log --oneline --parents --merges -n 88 | grep "Merge branch 'master' into plo-dev." | head -n 1 | cut -d" " -f3`

if [[ -z "${BASEREV}" ]]; then
    echo "Failed to find base revision for plo-dev branch."
    exit 1
fi

[[ -e "${DDIR}/lld-bbsections.patch" ]] && rm -f ${DDIR}/lld-bbsections.patch
for F in "${DD[@]}" ; do
    git diff "${BASEREV}" -- ${F} >> ${DDIR}/lld-bbsections.patch
done

echo "Generated ${DDIR}/lld-bbsections.patch"

popd 1>/dev/null 2>&1
