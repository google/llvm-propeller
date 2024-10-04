#
# Copyright 2024 The Propeller Authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

# Data files for Propeller tests

load("//propeller/bzl:list_symbols.bzl", "list_symbols")

package(
    default_applicable_licenses = ["//propeller:license"],
    default_visibility = ["//propeller:default_visibility"] + [
    ],
)

exports_files([
    "test_comdat_1.cc",
    "test_comdat_2.cc",
    "test_comdat.h",
])

# This rule generates a binary with comdat functions.
genrule(
    name = "test_comdat_data",
    srcs = [
        "test_comdat_1.cc",
        "test_comdat_2.cc",
        "test_comdat.h",
    ],
    outs = [
        "test_comdat.bin",
    ],
    cmd = "cp $(SRCS) . ; \
            $(CC) $(CC_FLAGS) -g -O0 test_comdat_1.cc test_comdat_2.cc -o $(RULEDIR)/test_comdat.bin",
    toolchains = [
        "@bazel_tools//tools/cpp:current_cc_toolchain",
        "@bazel_tools//tools/cpp:cc_flags",
    ],
)

list_symbols(
    name = "test_comdat.symmap",
    src = ":test_comdat.bin",
    address_radix = "dec",
    defined_only = True,
)

# This rule generates a split-dwarf binary with comdat functions.
genrule(
    name = "test_comdat_with_dwp_data",
    srcs = [
        "test_comdat_1.cc",
        "test_comdat_2.cc",
        "test_comdat.h",
    ],
    outs = [
        "test_comdat_with_dwp.bin",
        "test_comdat_with_dwp.dwp",
    ],
    cmd = "cp $(SRCS) . ;     $(CC) $(CC_FLAGS) -g -O0 -gsplit-dwarf test_comdat_1.cc test_comdat_2.cc     -o $(RULEDIR)/test_comdat_with_dwp.bin ;     $(location @llvm-project//llvm:llvm-dwp)       -e $(RULEDIR)/test_comdat_with_dwp.bin -o $(RULEDIR)/test_comdat_with_dwp.dwp",
    toolchains = [
        "@bazel_tools//tools/cpp:current_cc_toolchain",
        "@bazel_tools//tools/cpp:cc_flags",
    ],
    tools = ["@llvm-project//llvm:llvm-dwp"],
)
