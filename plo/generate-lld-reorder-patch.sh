#!/bin/bash

# Total 7 files.
declare -a DD=( ELF/PropellerBBReordering.h
                ELF/PropellerBBReordering.cpp
                ELF/PropellerFuncOrdering.h
                ELF/PropellerFuncOrdering.cpp
                test/ELF/propeller/propeller-opt-all-combinations.s
                test/ELF/propeller/propeller-layout-function-ordering.s
                test/ELF/propeller/propeller-layout-function-with-loop.s
                test/ELF/propeller/propeller-layout-optimal-fallthrough.s )

LLD_PATCH_NAME=reorder

source generate-lld-patch.inc
