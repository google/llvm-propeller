# Copyright 2025 The Propeller Authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Data files for Propeller tests

load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("//propeller/bzl:list_symbols.bzl", "list_symbols")
# [Google-internal load of `proto_data`]

package(
    default_applicable_licenses = ["//propeller:license"],
    default_testonly = True,
    default_visibility = ["//propeller:default_visibility"],
)

_LLVM_PROPELLER_TESTDATA_TOOLCHAINS = [
    "//propeller/bzl:toolchain_vars",
    "@bazel_tools//tools/cpp:current_cc_toolchain",
    "@bazel_tools//tools/cpp:cc_flags",
]

exports_files([
    "all_cold_landing_pads.protobuf",
    "bimodal_sample.bin",
    "bimodal_sample.cloning_cc_profile.txt",
    "bimodal_sample.x.bin",
    "bimodal_sample_mfs.bin",
    "bimodal_sample_mfs.cc_profile.txt",
    "bimodal_sample_mfs.cloning.cc_profile.txt",
    "bimodal_sample_mfs.cloning.ld_profile.txt",
    "bimodal_sample_mfs.ld_profile.txt",
    "bimodal_sample_v2.cloning_cc_profile.txt",
    "call_from_simple_loop.protobuf",
    "hot_and_cold_landing_pads.protobuf",
    "libro_sample.so",
    "llvm_function_samples.binary",
    "llvm_function_samples_perf.data",
    "loop_no_entry_no_exit.protobuf",
    "multiple_cold_blocks.protobuf",
    "nested_loop.protobuf",
    "propeller_sample.prefetch.txt",
    "propeller_sample.protobuf",
    "propeller_sample_1.bin",
    "propeller_sample_1_no_buildid.bin",  # propeller_sample_1.bin built without -Wl,-build-id.
    "propeller_sample_2.bin",
    "ro_sample.out",
    "sample.arm.bin",
    "sample.bin",
    "sample.c",
    "sample_O0.bin",
    "sample_cc_directives.arm.golden.txt",
    "sample_cc_directives.cloning.golden.txt",
    "sample_cc_directives.golden.txt",
    "sample_cc_directives.prefetch.golden.txt",
    "sample_cc_directives.prefetchonly.golden.txt",
    "sample_cc_directives.v0.golden.txt",
    "sample_different_buildid.bin",
    "sample_ld_directives.arm.golden.txt",
    "sample_ld_directives.golden.txt",
    "sample_pgo_analysis_map.bin",
    "sample_pgo_analysis_map_O0.bin",  # sample_pgo_analysis_map_bin built with -O0.
    "sample_section.bin",
    "sample_section_cc_directives.golden.txt",
    "sample_section_ld_directives.golden.txt",
    "sample_split_only_cc_directives.golden.txt",
    "sample_split_only_ld_directives.golden.txt",
    "sample_verbose_cc_directives.golden.txt",
    "sample_with_bb_hash.bin",
    "sample_with_bb_hash.perfdata",
    "sample_with_callsites.bin",  # generated using the "sample_bin" genrule.
    "sample_with_dropped_symbol.bin",
    "simple_conditionals_join.protobuf",
    "simple_loop.protobuf",
    "simple_multi_function.protobuf",
    "special_case.bin",
    "test_comdat.h",
    "test_comdat_1.cc",
    "test_comdat_2.cc",
    "three_branches.protobuf",
    "two_conditionals_in_loop.protobuf",
    "two_large_blocks.protobuf",
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
    cmd = "$(CC) $(CC_FLAGS) -O0 -gsplit-dwarf=split -gmlt -Wl,-build-id -pie -fbasic-block-address-map $< -o $(RULEDIR)/propeller_sample_1.bin.gen &&           $(location @llvm-project//llvm:llvm-dwp)           -e $(RULEDIR)/propeller_sample_1.bin.gen -o $(RULEDIR)/propeller_sample_1.dwp.gen $(RULEDIR)/propeller_sample_1.bin.gen-propeller_sample_1.dwo &&           cd $(RULEDIR) ; /usr/bin/perf record -o propeller_sample_1.perfdata1.gen -e cycles -b -- ./propeller_sample_1.bin.gen ;           /usr/bin/perf record -o propeller_sample_1.perfdata2.gen -e cycles -b -- ./propeller_sample_1.bin.gen 1 2 3 4",
    tags = [
        "manual",
    ],
    # Branch stack sampling (requested above with `-b`) is x86-only for now.
    target_compatible_with = ["@platforms//cpu:x86_64"],  # buildifier: disable=platform-specific-binaries
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
    $(CC) $(CC_FLAGS) -no-pie -Wl,--build-id -fbasic-block-address-map -x c++ -O0 $< \
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
    cmd = "$(CC) $(CC_FLAGS) -pie -Wl,--build-id=none -fbasic-block-address-map -x c++ -O0 $< -o $@",
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
    cmd = "export ALL_FLAGS=\"$(CC_FLAGS) -O0 -fbasic-block-address-map -funique-internal-linkage-names\"; " +
          "$(CC) $$ALL_FLAGS -c -DVER1 -o $(RULEDIR)/uniq_ver1.o $< && " +
          "$(CC) $$ALL_FLAGS -c -DVER2 -o $(RULEDIR)/uniq_ver2.o $< && " +
          "$(CC) $$ALL_FLAGS -c -DMAIN -o $(RULEDIR)/uniq_main.o $< && " +
          "$(CC) $$ALL_FLAGS $(RULEDIR)/uniq_{ver1,ver2,main}.o -o $@",
    toolchains = _LLVM_PROPELLER_TESTDATA_TOOLCHAINS,
)

# This rule generates a bimodal binary which will run in one of the two loops depending on the
# input. The is a bit more complex than bimodal_sample.c and has more cloning opportunities. This
# requires LBR support. Use `--spawn_strategy=local` when the local machine supports LBR.
genrule(
    name = "bimodal_sample_v2_bin",
    srcs = ["bimodal_sample_v2.c"],
    outs = [
        "bimodal_sample_v2.bin.gen",
        "bimodal_sample_v2.perfdata.1.gen",
        "bimodal_sample_v2.perfdata.2.gen",
    ],
    cmd = "$(CC) $(CC_FLAGS) -O2 -Wl,-build-id -pie -fbasic-block-address-map $< -o " +
          "$(RULEDIR)/bimodal_sample_v2.bin.gen && " +
          "cd $(RULEDIR) ; " +
          "/usr/bin/perf record -o bimodal_sample_v2.perfdata.1.gen -e cycles -b -- " +
          "./bimodal_sample_v2.bin.gen 1 && " +
          "/usr/bin/perf record -o bimodal_sample_v2.perfdata.2.gen -e cycles -b -- " +
          "./bimodal_sample_v2.bin.gen 1 2",
    tags = [
        "manual",
    ],
    # Branch stack sampling (requested above with `-b`) is x86-only for now.
    target_compatible_with = ["@platforms//cpu:x86_64"],  # buildifier: disable=platform-specific-binaries
    toolchains = _LLVM_PROPELLER_TESTDATA_TOOLCHAINS,
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
    cmd = "$(CC) $(CC_FLAGS) -O2 -Wl,-build-id -pie -fbasic-block-address-map $< -o " +
          "$(RULEDIR)/bimodal_sample.bin.gen && " +
          "cd $(RULEDIR) ; " +
          "/usr/bin/perf record -o bimodal_sample.perfdata.1.gen -e cycles -b -- " +
          "./bimodal_sample.bin.gen 1 && " +
          "/usr/bin/perf record -o bimodal_sample.perfdata.2.gen -e cycles -b -- " +
          "./bimodal_sample.bin.gen 1 2",
    tags = [
        "manual",
    ],
    # Branch stack sampling (requested above with `-b`) is x86-only for now.
    target_compatible_with = ["@platforms//cpu:x86_64"],  # buildifier: disable=platform-specific-binaries
    toolchains = _LLVM_PROPELLER_TESTDATA_TOOLCHAINS,
)

# This rule generates a bimodal binary which will run in one of the two loops or both loops
# depending on the input. This requires LBR support. Use `--spawn_strategy=local` when the local
# machine supports LBR.
genrule(
    name = "bimodal_sample_x_bin",
    srcs = ["bimodal_sample.x.c"],
    outs = [
        "bimodal_sample.x.bin.gen",
        "bimodal_sample.x.perfdata.1.gen",  # profile over loop 1.
        "bimodal_sample.x.perfdata.2.gen",  # profile over loop 2.
        "bimodal_sample.x.perfdata.combined.gen",  # profile over loop 1 and loop 2.
    ],
    cmd = "$(CC) $(CC_FLAGS) -O2 -Wl,-build-id -pie -fbasic-block-address-map $< -o " +
          "$(RULEDIR)/bimodal_sample.x.bin.gen && " +
          "cd $(RULEDIR) ; " +
          "/usr/bin/perf record -o bimodal_sample.x.perfdata.1.gen -e cycles -b -- " +
          "./bimodal_sample.x.bin.gen && " +
          "/usr/bin/perf record -o bimodal_sample.x.perfdata.2.gen -e cycles -b -- " +
          "./bimodal_sample.x.bin.gen 1 && " +
          "/usr/bin/perf record -o bimodal_sample.x.perfdata.combined.gen -e cycles -b " +
          "-- ./bimodal_sample.x.bin.gen 1 2",
    tags = [
        "manual",
    ],
    # Branch stack sampling (requested above with `-b`) is x86-only for now.
    target_compatible_with = ["@platforms//cpu:x86_64"],  # buildifier: disable=platform-specific-binaries
    toolchains = _LLVM_PROPELLER_TESTDATA_TOOLCHAINS,
)

cc_binary(
    name = "sample",
    srcs = ["sample.c"],
    deps = [
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/flags:usage",
    ],
)

cc_binary(
    name = "thunks",
    srcs = ["thunks.c"],
    deps = [
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/flags:usage",
    ],
)

# This rule is used to manually generate sample.bin and sample.perfdata.
genrule(
    name = "sample_bin",
    srcs = ["sample.c"],
    outs = [
        "sample.bin.gen",
        "sample.perfdata.gen",
    ],
    # Starts 3 sample.bin in parallel and collect profile.
    cmd = "$(CC) $(CC_FLAGS) -g -O2 -Wl,-build-id -pie -fbasic-block-address-map $< -o " +
          "$(RULEDIR)/sample.bin.gen && " +
          "cd $(RULEDIR) ; echo '1 2 3' > sample.scratch ; " +
          "/usr/bin/perf record -o sample.perfdata.gen -e cycles -b -- " +
          "sh -c 'cat sample.scratch | xargs -n 1 -P3 ./sample.bin.gen'",
    tags = [
        "manual",
    ],
    # Branch stack sampling (requested above with `-b`) is x86-only for now.
    target_compatible_with = ["@platforms//cpu:x86_64"],  # buildifier: disable=platform-specific-binaries
    toolchains = _LLVM_PROPELLER_TESTDATA_TOOLCHAINS,
)

# This rule is used to manually generate sample.bin with basic block hashes and its sample.perfdata.
genrule(
    name = "sample_with_bb_hash_bin",
    srcs = ["sample.c"],
    outs = [
        "sample_with_bb_hash.bin.gen",
        "sample_with_bb_hash.perfdata.gen",
    ],
    cmd = "$(CC) $(CC_FLAGS) -g -O2 -Wl,-build-id -pie -fbasic-block-address-map -mllvm -emit-bb-hash $< -o " +
          "$(RULEDIR)/sample_with_bb_hash.bin.gen && " +
          "cd $(RULEDIR) ; echo '1 2 3' > sample.scratch ; " +
          "/usr/bin/perf record -o sample_with_bb_hash.perfdata.gen -e cycles -b -- " +
          "sh -c 'cat sample.scratch | xargs -n 1 -P3 ./sample_with_bb_hash.bin.gen'",
    tags = [
        "manual",
    ],
    # Branch stack sampling (requested above with `-b`) is x86-only for now.
    target_compatible_with = ["@platforms//cpu:x86_64"],  # buildifier: disable=platform-specific-binaries
    toolchains = _LLVM_PROPELLER_TESTDATA_TOOLCHAINS,
)

# This rule is used to manually generate sample_with_dropped_symbol.bin which is the same as
# sample.bin but with the main symbol dropped.
genrule(
    name = "sample_with_dropped_symbol_bin",
    srcs = ["sample.c"],
    outs = [
        "sample_with_dropped_symbol_bin.bin.gen",
    ],
    cmd = "$(CC) $(CC_FLAGS) -g -O2 -Wl,-build-id -pie -fbasic-block-address-map $< -o " +
          "$(RULEDIR)/sample.bin.gen && " +
          "$(OBJCOPY) --strip-symbol=main $(RULEDIR)/sample.bin.gen " +
          "$(RULEDIR)/sample_with_dropped_symbol_bin.bin.gen",
    tags = [
        "manual",
    ],
    toolchains = _LLVM_PROPELLER_TESTDATA_TOOLCHAINS,
)

# This rule is used to manually generate a binary with special cases of address mapping (empty basic
# blocks and a call followed by nop).
genrule(
    name = "special_case_bin",
    srcs = ["special_case.c"],
    outs = [
        "special_case.bin.gen",
    ],
    cmd = "$(CC) -O1 -mllvm -align-all-blocks=4 -fbasic-block-address-map $< -o $(RULEDIR)/special_case.bin.gen",
    tags = [
        "manual",
    ],
    toolchains = _LLVM_PROPELLER_TESTDATA_TOOLCHAINS,
)

# This rule is used to manually generate a bimodal MFS binary which is FDO-optimized through one
# input mode, and a LBR profile from that MFS binary with both input modes.
genrule(
    name = "bimodal_sample_mfs_bin",
    srcs = ["bimodal_sample.c"],
    outs = [
        "bimodal_sample_mfs.bin.gen",
        "bimodal_sample_mfs.perfdata.gen",
    ],
    cmd = "rm -rf $(RULEDIR)/profile; " +
          "$(CC) $(CC_FLAGS) -g -O2 -pie -fprofile-generate=$(RULEDIR)/profile $< -o " +
          "sample_inst.bin.gen ; " +
          "./sample_inst.bin.gen ; " +
          "$(LLVM_PROFDATA) merge $(RULEDIR)/profile/* -o $(RULEDIR)/profile/default.profdata ; " +
          "$(CC) $(CC_FLAGS) -g -O2 -Wl,-build-id -pie -fprofile-use=$(RULEDIR)/profile " +
          "-fbasic-block-address-map -fsplit-machine-functions $< -o " +
          "$(RULEDIR)/bimodal_sample_mfs.bin.gen ; " +
          "if [[ `$(NM) $(RULEDIR)/bimodal_sample_mfs.bin.gen | grep compute | wc -l` != 2 ]]; " +
          "then exit 1; fi; " +
          "cd $(RULEDIR) ; echo '1' > sample.scratch ; echo '1 2' >> sample.scratch ; " +
          "/usr/bin/perf record -o bimodal_sample_mfs.perfdata.gen -e cycles -b -- " +
          "sh -c 'cat sample.scratch | xargs -L1 -P2 ./bimodal_sample_mfs.bin.gen'",
    tags = [
        "manual",
    ],
    # Branch stack sampling (requested above with `-b`) is x86-only for now.
    target_compatible_with = ["@platforms//cpu:x86_64"],  # buildifier: disable=platform-specific-binaries
    toolchains = _LLVM_PROPELLER_TESTDATA_TOOLCHAINS,
)

# This rule is used to manually generate a BBAddrMap binary with PGO analysis map.
genrule(
    name = "sample_pgo_analysis_map_bin",
    srcs = ["sample.c"],
    outs = [
        "sample_pgo_analysis_map.bin.gen",
    ],
    cmd = "$(CC) $(CC_FLAGS) -g -O2 -Wl,-build-id -pie -fbasic-block-address-map " +
          "-mllvm=-pgo-analysis-map=all $< -o $(RULEDIR)/sample_pgo_analysis_map.bin.gen",
    tags = [
        "manual",
    ],
    toolchains = _LLVM_PROPELLER_TESTDATA_TOOLCHAINS,
)

genrule(
    name = "duplicate_symbols_bin",
    srcs = [
        "duplicate_symbol_1.c",
        "duplicate_symbol_2.c",
    ],
    outs = ["duplicate_symbols.bin"],
    cmd = "$(CC) $(CC_FLAGS) -fbasic-block-address-map -O0 $(SRCS) -o $@",
    toolchains = _LLVM_PROPELLER_TESTDATA_TOOLCHAINS,
)

# This rule generates a binary with functions in four sections (.text, .text.unlikely,
# .anycall.anysection, and .othercall.othersection), and a profile containing samples from three of
# these sections (.text, .anycall.anysection, and .othercall.othersection).
# This requires LBR support.  Use `--spawn_strategy=local` when the local machine supports LBR.
genrule(
    name = "sample_section_bin",
    srcs = ["sample_section.c"],
    outs = [
        "sample_section.bin.gen",
        "sample_section.perfdata.gen",
    ],
    cmd = "$(CC) $(CC_FLAGS) -g -O2 -Wl,-build-id -no-pie -Wl,-z,keep-text-section-prefix " +
          "-fbasic-block-address-map $< -o " +
          "$(RULEDIR)/sample_section.bin.gen && " +
          "cd $(RULEDIR) ; " +
          "/usr/bin/perf record -o sample_section.perfdata.gen -e cycles -b " +
          "-- ./sample_section.bin.gen",
    tags = [
        "manual",
    ],
    # Branch stack sampling (requested above with `-b`) is x86-only for now.
    target_compatible_with = ["@platforms//cpu:x86_64"],  # buildifier: disable=platform-specific-binaries
    toolchains = _LLVM_PROPELLER_TESTDATA_TOOLCHAINS,
)

# This rule can be used to manually generate sample.arm.bin and
# sample.arm.perfdata.
# Do not make this into the deps of tests since regenerating the file every time is cumbersome.
#
# To build `sample_arm_bin`, you need to target Arm. If building with Propeller
# annotations, pass `--host_features=propeller_annotate` when building the genrule.
genrule(
    name = "sample_arm_bin",
    outs = [
        "sample.arm.bin.gen",
        "sample.arm.perfdata.gen",
    ],
    # Starts 3 sample.arm.bin in parallel and collect profile.
    cmd = "cp $(locations sample) " +
          "$(RULEDIR)/sample.arm.bin.gen && " +
          "cd $(RULEDIR) ; echo '1 2 3' > sample.arm.scratch ; " +
          "perf5 record -o sample.arm.perfdata.gen " +
          "-e 'arm_spe_0/branch_filter=1,load_filter=0,store_filter=0,jitter=1,ts_enable=1/' " +
          "--all-user -c 10007 -N -- " +
          "sh -c 'cat sample.arm.scratch | xargs -n 1 -P3 ./sample.arm.bin.gen'",
    exec_compatible_with = ["@platforms//cpu:arm64"],
    tags = [
        "manual",
        "requires-arch:arm",
    ],
    target_compatible_with = ["@platforms//cpu:aarch64"],
    tools = [":sample"],
)

# This rule can be used to manually generate thunks.bin.
#
# To build `thunks_bin`, you need to target Arm.
genrule(
    name = "thunks_bin",
    srcs = [
        "thunks.c",
        "thunks.lds",
    ],
    outs = [
        "thunks.bin.gen",
        "thunks.perfdata.gen",
    ],
    cmd = "$(CC) $(CC_FLAGS) -g -O2 -Wl,-build-id -pie -fbasic-block-address-map " +
          "-Wl,-z,keep-text-section-prefix -Wl,-T,$(location thunks.lds) $(location thunks.c) -o " +
          "$(RULEDIR)/thunks.bin.gen ;" +
          "cd $(RULEDIR) ; echo '1 2 3' > thunks.scratch ; " +
          "perf5 record -o thunks.perfdata.gen " +
          "-e 'arm_spe_0/branch_filter=1,load_filter=0,store_filter=0,jitter=1,ts_enable=1/' " +
          "--all-user -c 10007 -N -- " +
          "sh -c 'cat thunks.scratch | xargs -n 1 -P3 ./thunks.bin.gen'",
    exec_compatible_with = ["@platforms//cpu:arm64"],
    tags = [
        "manual",
        "requires-arch:arm",
    ],
    target_compatible_with = ["@platforms//cpu:aarch64"],
    toolchains = _LLVM_PROPELLER_TESTDATA_TOOLCHAINS,
)
