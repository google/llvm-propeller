#!/bin/bash

# Total 28 files.
declare -a THE_FILES=( ELF/Propeller.h
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

LLD_PATCH_NAME=propeller

source generate-lld-patch.inc

