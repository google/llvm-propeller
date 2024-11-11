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

load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@rules_license//rules:license.bzl", "license")

package(
    default_applicable_licenses = [":license"],
    default_visibility = [":default_visibility"],
    # Legacy deprecated licenses rule.
    # Should be kept until transition to new license rules is complete.
    licenses = ["notice"],
)

# Propeller, a profile-guided, relinking optimizer for warehouse-scale applications.
package_group(
    name = "default_visibility",
    packages = [
        "//propeller/...",
    ],
)

# Define the license for this package, which is used as the default license
# for all targets in this package based on default_applicable_licenses above.
license(name = "license")

exports_files(["LICENSE"])

###############
#  Libraries  #
###############
cc_library(
    name = "binary_address_branch",
    hdrs = ["binary_address_branch.h"],
    deps = ["@abseil-cpp//absl/strings:str_format"],
)

cc_library(
    name = "branch_aggregation",
    srcs = ["branch_aggregation.cc"],
    hdrs = ["branch_aggregation.h"],
    deps = [
        ":binary_address_branch",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
    ],
)

cc_library(
    name = "branch_frequencies",
    srcs = ["branch_frequencies.cc"],
    hdrs = ["branch_frequencies.h"],
    deps = [
        ":binary_address_branch",
        ":branch_frequencies_cc_proto",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/container:flat_hash_map",
    ],
)

proto_library(
    name = "branch_frequencies_proto",
    srcs = ["branch_frequencies.proto"],
)

cc_proto_library(
    name = "branch_frequencies_cc_proto",
    deps = [":branch_frequencies_proto"],
)

cc_library(
    name = "lbr_aggregation",
    hdrs = ["lbr_aggregation.h"],
    deps = [
        ":binary_address_branch",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/container:flat_hash_map",
    ],
)

cc_library(
    name = "addr2cu",
    srcs = ["addr2cu.cc"],
    hdrs = ["addr2cu.h"],
    deps = [
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@llvm-project//llvm:BinaryFormat",
        "@llvm-project//llvm:DebugInfoDWARF",
        "@llvm-project//llvm:Object",
    ],
)

cc_library(
    name = "status_macros",
    hdrs = ["status_macros.h"],
    deps = [
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "lazy_evaluator",
    hdrs = ["lazy_evaluator.h"],
    deps = [
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/functional:overload",
    ],
)

cc_library(
    name = "bb_handle",
    hdrs = ["bb_handle.h"],
    deps = ["@abseil-cpp//absl/strings:str_format"],
)

cc_library(
    name = "spe_pid_provider",
    hdrs = ["spe_pid_provider.h"],
    deps = [
        "@abseil-cpp//absl/status:statusor",
        "@com_google_perf_data_converter//src/quipper:arm_spe_decoder",
    ],
)

cc_library(
    name = "spe_tid_pid_provider",
    srcs = ["spe_tid_pid_provider.cc"],
    hdrs = ["spe_tid_pid_provider.h"],
    deps = [
        ":spe_pid_provider",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_google_perf_data_converter//src/quipper:arm_spe_decoder",
        "@com_google_perf_data_converter//src/quipper:perf_data_cc_proto",
        "@com_google_perf_data_converter//src/quipper:perf_data_utils",
        "@com_google_protobuf//:protobuf_lite",
    ],
)

cc_library(
    name = "binary_content",
    srcs = ["binary_content.cc"],
    hdrs = ["binary_content.h"],
    deps = [
        ":addr2cu",
        ":status_macros",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@llvm-project//llvm:BinaryFormat",
        "@llvm-project//llvm:DebugInfo",
        "@llvm-project//llvm:Object",
        "@llvm-project//llvm:Support",
    ],
)

cc_library(
    name = "cfg_edge_kind",
    srcs = ["cfg_edge_kind.cc"],
    hdrs = [
        "cfg_edge_kind.h",
    ],
    deps = ["@abseil-cpp//absl/log"],
)

cc_library(
    name = "cfg_id",
    hdrs = [
        "cfg_id.h",
    ],
    deps = ["@abseil-cpp//absl/strings:str_format"],
)

cc_library(
    name = "perf_data_provider",
    hdrs = ["perf_data_provider.h"],
    deps = [
        ":status_macros",
        "@abseil-cpp//absl/status:statusor",
        "@llvm-project//llvm:Support",
    ],
)

proto_library(
    name = "propeller_options_proto",
    srcs = ["propeller_options.proto"],
)

cc_proto_library(
    name = "propeller_options_cc_proto",
    deps = [":propeller_options_proto"],
)

cc_library(
    name = "resolve_mmap_name",
    srcs = ["resolve_mmap_name.cc"],
    hdrs = ["resolve_mmap_name.h"],
    deps = [":propeller_options_cc_proto"],
)

cc_library(
    name = "file_perf_data_provider",
    srcs = ["file_perf_data_provider.cc"],
    hdrs = ["file_perf_data_provider.h"],
    deps = [
        ":perf_data_provider",
        ":status_macros",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@llvm-project//llvm:Support",
    ],
)

cc_library(
    name = "cfg_edge",
    hdrs = [
        "cfg_edge.h",
    ],
    deps = [
        ":cfg_edge_kind",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_library(
    name = "cfg_node",
    srcs = ["cfg_node.cc"],
    hdrs = ["cfg_node.h"],
    deps = [
        ":cfg_edge",
        ":cfg_edge_kind",
        ":cfg_id",
        "@abseil-cpp//absl/functional:function_ref",
        "@abseil-cpp//absl/strings",
        "@llvm-project//llvm:Object",
    ],
)

cc_library(
    name = "cfg",
    srcs = ["cfg.cc"],
    hdrs = [
        "cfg.h",
    ],
    deps = [
        ":cfg_edge",
        ":cfg_edge_kind",
        ":cfg_id",
        ":cfg_node",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/functional:function_ref",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/types:span",
        "@llvm-project//llvm:Support",
    ],
)

proto_library(
    name = "cfg_proto",
    srcs = ["cfg.proto"],
)

cc_proto_library(
    name = "cfg_cc_proto",
    deps = ["cfg_proto"],
)

cc_library(
    name = "program_cfg",
    srcs = ["program_cfg.cc"],
    hdrs = ["program_cfg.h"],
    deps = [
        ":cfg",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@llvm-project//llvm:Support",
    ],
)

cc_library(
    name = "chain_merge_order",
    hdrs = ["chain_merge_order.h"],
    deps = [
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_library(
    name = "propeller_statistics",
    srcs = ["propeller_statistics.cc"],
    hdrs = ["propeller_statistics.h"],
    deps = [
        ":cfg_edge_kind",
        ":chain_merge_order",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

########################
#  Tests & Test Utils  #
########################
cc_library(
    name = "parse_text_proto",
    testonly = True,
    hdrs = ["parse_text_proto.h"],
    deps = [
        "@abseil-cpp//absl/log:absl_check",
        "@abseil-cpp//absl/strings:string_view",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "protocol_buffer_matchers",
    testonly = True,
    hdrs = ["protocol_buffer_matchers.h"],
    deps = [
        "@abseil-cpp//absl/memory",
        "@com_google_googletest//:gtest",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "cfg_matchers",
    testonly = True,
    hdrs = ["cfg_matchers.h"],
    deps = [
        ":cfg",
        ":cfg_edge",
        ":cfg_edge_kind",
        ":cfg_id",
        "@abseil-cpp//absl/types:span",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "cfg_testutil",
    srcs = ["cfg_testutil.cc"],
    hdrs = ["cfg_testutil.h"],
    deps = [
        ":cfg",
        ":cfg_edge",
        ":cfg_edge_kind",
        ":cfg_node",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/types:span",
        "@llvm-project//llvm:Object",
        "@llvm-project//llvm:Support",
    ],
)

cc_library(
    name = "mock_program_cfg_builder",
    testonly = True,
    srcs = ["mock_program_cfg_builder.cc"],
    hdrs = ["mock_program_cfg_builder.h"],
    deps = [
        ":cfg",
        ":cfg_cc_proto",
        ":cfg_edge",
        ":cfg_edge_kind",
        ":cfg_id",
        ":cfg_node",
        ":cfg_testutil",
        ":program_cfg",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
        "@com_google_protobuf//:protobuf",
        "@llvm-project//llvm:Object",
        "@llvm-project//llvm:Support",
    ],
)

cc_library(
    name = "status_testing_macros",
    testonly = True,
    hdrs = ["status_testing_macros.h"],
    deps = [
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@com_google_googletest//:gtest",
    ],
)

cc_test(
    name = "status_testing_macros_test",
    srcs = ["status_testing_macros_test.cc"],
    deps = [
        ":status_testing_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "status_macros_test",
    srcs = ["status_macros_test.cc"],
    deps = [
        ":status_macros",
        ":status_testing_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "branch_aggregation_test",
    srcs = ["branch_aggregation_test.cc"],
    deps = [
        ":branch_aggregation",
        ":status_testing_macros",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "branch_frequencies_test",
    srcs = ["branch_frequencies_test.cc"],
    deps = [
        ":branch_frequencies",
        ":branch_frequencies_cc_proto",
        ":parse_text_proto",
        ":protocol_buffer_matchers",
        ":status_testing_macros",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "addr2cu_test",
    srcs = ["addr2cu_test.cc"],
    data = [
        "//propeller/testdata:test_comdat.symmap",
        "//propeller/testdata:test_comdat_data",
        "//propeller/testdata:test_comdat_with_dwp.symmap",
        "//propeller/testdata:test_comdat_with_dwp_data",
    ],
    deps = [
        ":addr2cu",
        ":status_testing_macros",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@com_google_googletest//:gtest_main",
        "@llvm-project//llvm:DebugInfoDWARF",
        "@llvm-project//llvm:Object",
        "@llvm-project//llvm:Support",
    ],
)

cc_test(
    name = "lazy_evaluator_test",
    srcs = ["lazy_evaluator_test.cc"],
    deps = [
        ":lazy_evaluator",
        ":status_testing_macros",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "spe_tid_pid_provider_test",
    srcs = ["spe_tid_pid_provider_test.cc"],
    deps = [
        ":parse_text_proto",
        ":spe_tid_pid_provider",
        ":status_testing_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@com_google_googletest//:gtest_main",
        "@com_google_perf_data_converter//src/quipper:perf_data_cc_proto",
        "@com_google_perf_data_converter//src/quipper:perf_parser",
        "@com_google_protobuf//:protobuf_lite",
    ],
)

cc_test(
    name = "binary_content_test",
    srcs = ["binary_content_test.cc"],
    data = [
        "//propeller/testdata:llvm_function_samples.binary",
        "//propeller/testdata:propeller_barebone_nopie_buildid",
        "//propeller/testdata:propeller_barebone_pie_nobuildid_bin",
        "//propeller/testdata:propeller_sample_1.bin",
    ],
    deps = [
        ":binary_content",
        ":status_testing_macros",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/strings",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "file_perf_data_provider_test",
    srcs = ["file_perf_data_provider_test.cc"],
    deps = [
        ":file_perf_data_provider",
        ":status_testing_macros",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/strings",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "cfg_test",
    srcs = [
        "cfg_test.cc",
    ],
    deps = [
        ":cfg",
        ":cfg_edge_kind",
        ":cfg_id",
        ":cfg_matchers",
        ":cfg_testutil",
        ":status_testing_macros",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "program_cfg_test",
    srcs = [
        "program_cfg_test.cc",
    ],
    data = [
        "//propeller/testdata:propeller_sample.protobuf",
    ],
    deps = [
        ":cfg",
        ":cfg_edge",
        ":cfg_edge_kind",
        ":mock_program_cfg_builder",
        ":program_cfg",
        ":status_testing_macros",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/strings",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "propeller_statistics_test",
    srcs = ["propeller_statistics_test.cc"],
    deps = [
        ":cfg_edge_kind",
        ":propeller_statistics",
        ":status_testing_macros",
        "@com_google_googletest//:gtest_main",
    ],
)
