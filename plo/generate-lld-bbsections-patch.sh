#!/bin/bash

# Total 20 files.
declare -a THE_FILES=( Common/Args.cpp
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

LLD_PATCH_NAME=bbsections
source generate-lld-patch.inc


