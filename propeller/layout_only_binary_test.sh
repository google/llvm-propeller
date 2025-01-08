#!/bin/bash

source gbash.sh || exit
source module gbash_unit.sh

layout_only_readelf="${TEST_SRCDIR}/google3/third_party\
/llvm_propeller/testdata/layout_only_binary_readelf.txt"
no_layout_only_readelf="${TEST_SRCDIR}/google3/third_party\
/llvm_propeller/testdata/no_layout_only_binary_readelf.txt"

# Check that readelf_out does not contain a .text.split section or only contains
# an empty one like below:
#  [19] .text.split PROGBITS 00000000000019c5 0009c5 000000 00 AXR
function test::layout_only() {
  if ! grep -E ".text.split[[:space:]]+PROGBITS" "$layout_only_readelf" || \
    grep -inE "\.text\.split[[:space:]]+PROGBITS[[:space:]]+[0-9a-f]+\
[[:space:]]+[0-9a-f]+[[:space:]]+0+[[:space:]]" "$layout_only_readelf"; then
    echo "PASS"
    return
  fi
  die "non-empty .text.split found in layout_only binary"
}

# Check that readelf_out has a non-empty .text.split section line like:
# [19] .text.split PROGBITS 0000000000001965 000965 000074 ...
function test::no_layout_only() {
  if grep -E ".text.split[[:space:]]+PROGBITS" "$no_layout_only_readelf" && \
    ! grep -inE "\.text\.split[[:space:]]+PROGBITS[[:space:]]+[0-9a-f]+\
[[:space:]]+[0-9a-f]+[[:space:]]+0+[[:space:]]" "$no_layout_only_readelf"; then
    echo "PASS"
    return
  fi
  die "no .text.split or empty .text.split found in no_layout_only binary"
}

gbash::unit::main "$@"
