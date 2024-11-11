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
    default_visibility = ["//propeller:default_visibility"],
)

_LLVM_PROPELLER_TESTDATA_TOOLCHAINS = [
    "//propeller/bzl:toolchain_vars",
    "@bazel_tools//tools/cpp:current_cc_toolchain",
    "@bazel_tools//tools/cpp:cc_flags",
]

exports_files([
    "llvm_function_samples.binary",
    "llvm_function_samples_perf.data",
    "propeller_sample.protobuf",
    "propeller_sample_1.bin",
    "test_comdat.h",
    "test_comdat_1.cc",
    "test_comdat_2.cc",
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
    cmd = "cp $(SRCS) . ;             $(CC) $(CC_FLAGS) -g -O0 $(location test_comdat_1.cc) $(location test_comdat_2.cc) -o $(RULEDIR)/test_comdat.bin",
    toolchains = _LLVM_PROPELLER_TESTDATA_TOOLCHAINS,
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
    cmd = "cp $(SRCS) . ;     $(CC) $(CC_FLAGS) -g -O0 -gsplit-dwarf $(location test_comdat_1.cc) $(location test_comdat_2.cc)     -o $(RULEDIR)/test_comdat_with_dwp.bin ;     $(location @llvm-project//llvm:llvm-dwp)       -e $(RULEDIR)/test_comdat_with_dwp.bin -o $(RULEDIR)/test_comdat_with_dwp.dwp",
    toolchains = _LLVM_PROPELLER_TESTDATA_TOOLCHAINS,
    tools = ["@llvm-project//llvm:llvm-dwp"],
)

list_symbols(
    name = "test_comdat_with_dwp.symmap",
    src = ":test_comdat_with_dwp.bin",
    address_radix = "dec",
    defined_only = True,
)

# This rule is used to manually generate propeller_sample_1.bin, propeller_sample_1.perfdata1 and propeller_sample_1.perfdata2.
genrule(
    name = "propeller_sample_1_perfdata",
    srcs = ["propeller_sample_1.c"],
    outs = [
        "propeller_sample_1.bin.gen",
        "propeller_sample_1.dwp.gen",
        "propeller_sample_1.perfdata1.gen",
        "propeller_sample_1.perfdata2.gen",
    ],
    cmd = "$(CC) $(CC_FLAGS) -O0 -gsplit-dwarf=split -gmlt -Wl,-build-id -pie -fbasic-block-sections=labels $< -o $(RULEDIR)/propeller_sample_1.bin.gen && mv propeller_sample_1.dwo $(RULEDIR)/ && " +
          "$(location //third_party/llvm/llvm-project/llvm:llvm-dwp) -e $(RULEDIR)/propeller_sample_1.bin.gen -o $(RULEDIR)/propeller_sample_1.dwp.gen $(RULEDIR)/propeller_sample_1.dwo && " +
          "cd $(RULEDIR) ; /usr/bin/perf record -o propeller_sample_1.perfdata1.gen -e cycles -b -- ./propeller_sample_1.bin.gen ;" +
          "/usr/bin/perf record -o propeller_sample_1.perfdata2.gen -e cycles -b -- ./propeller_sample_1.bin.gen 1 2 3 4",
    tags = [
        "manual",
    ],
    # Branch stack sampling (requested above with `-b`) is x86-only for now.
    target_compatible_with = ["//third_party/bazel_platforms/cpu:x86_64"],  # buildifier: disable=platform-specific-binaries
    toolchains = _LLVM_PROPELLER_TESTDATA_TOOLCHAINS,
    tools = ["@llvm-project//llvm:llvm-dwp"],
)

genrule(
    name = "propeller_barebone_nopie_buildid",
    srcs = ["propeller_barebone.cc"],
    outs = [
        "propeller_barebone_nopie_buildid.bin",
        "propeller_barebone_nopie_buildid.build-id",
    ],
    cmd = """
    $(CC) $(CC_FLAGS) -no-pie -Wl,--build-id -fbasic-block-sections=labels -x c++ -O0 $< \
        -o $(RULEDIR)/propeller_barebone_nopie_buildid.bin
    $(READELF) --wide --hex-dump=.note.gnu.build-id \
        $(RULEDIR)/propeller_barebone_nopie_buildid.bin | \
        tail -n 1 | cut -d' ' -f2-3 | tr -d ' ' > \
        $(RULEDIR)/propeller_barebone_nopie_buildid.build-id
    """,
    # This genrule generates an x86 binary.
    toolchains = _LLVM_PROPELLER_TESTDATA_TOOLCHAINS,
)

genrule(
    name = "propeller_barebone_pie_nobuildid_bin",
    srcs = ["propeller_barebone.cc"],
    outs = ["propeller_barebone_pie_nobuildid.bin"],
    cmd = "$(CC) $(CC_FLAGS) -pie -Wl,--build-id=none -fbasic-block-sections=labels -x c++ -O0 $< -o $@",
    # This genrule generates an x86 binary.
    toolchains = _LLVM_PROPELLER_TESTDATA_TOOLCHAINS,
)

# This rule generates "a.out" containing duplicate unique-named functions.
genrule(
    name = "duplicate_unique_names",
    srcs = ["unique_names.cc"],
    outs = [
        "duplicate_unique_names.out",
    ],
    cmd = "export ALL_FLAGS=\"$(CC_FLAGS) -O0 -fbasic-block-sections=labels -funique-internal-linkage-names\"; " +
          "$(CC) $$ALL_FLAGS -c -DVER1 -o $(RULEDIR)/uniq_ver1.o $< && " +
          "$(CC) $$ALL_FLAGS -c -DVER2 -o $(RULEDIR)/uniq_ver2.o $< && " +
          "$(CC) $$ALL_FLAGS -c -DMAIN -o $(RULEDIR)/uniq_main.o $< && " +
          "$(CC) $$ALL_FLAGS $(RULEDIR)/uniq_{ver1,ver2,main}.o -o $@",
    toolchains = [
        "@bazel_tools//tools/cpp:current_cc_toolchain",
        "@bazel_tools//tools/cpp:cc_flags",
    ],
)

# This rule generates a bimodal binary which will run in one of the two loops depending on the
# input. This requires LBR support.  Use `--spawn_strategy=local` when the local machine supports
# LBR.
genrule(
    name = "bimodal_sample_bin",
    srcs = ["bimodal_sample.c"],
    outs = [
        "bimodal_sample.bin.gen",
        "bimodal_sample.perfdata.1.gen",
        "bimodal_sample.perfdata.2.gen",
    ],
    cmd = "$(CC) $(CC_FLAGS) -O2 -Wl,-build-id -pie -fbasic-block-sections=labels $< -o " +
          "$(RULEDIR)/bimodal_sample.bin.gen && " +
          "cd $(RULEDIR) ; " +
          "/usr/bin/perf record -o bimodal_sample.perfdata.1.gen -e cycles -b -- " +
          "./bimodal_sample.bin.gen && " +
          "/usr/bin/perf record -o bimodal_sample.perfdata.2.gen -e cycles -b -- " +
          "./bimodal_sample.bin.gen 1",
    tags = [
        "manual",
    ],
    # Branch stack sampling (requested above with `-b`) is x86-only for now.
    target_compatible_with = ["//third_party/bazel_platforms/cpu:x86_64"],  # buildifier: disable=platform-specific-binaries
    toolchains = [
        "@bazel_tools//tools/cpp:current_cc_toolchain",
        "@bazel_tools//tools/cpp:cc_flags",
    ],
)
