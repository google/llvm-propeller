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

load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@com_google_protobuf//bazel:proto_library.bzl", "proto_library")
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
    name = "branch_aggregator",
    hdrs = ["branch_aggregator.h"],
    deps = [
        ":binary_address_mapper",
        ":branch_aggregation",
        ":propeller_options_cc_proto",
        ":propeller_statistics",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/status:statusor",
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
    name = "branch_frequencies_aggregator",
    hdrs = ["branch_frequencies_aggregator.h"],
    deps = [
        ":binary_content",
        ":branch_frequencies",
        ":propeller_options_cc_proto",
        ":propeller_statistics",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "proto_branch_frequencies_aggregator",
    srcs = ["proto_branch_frequencies_aggregator.cc"],
    hdrs = ["proto_branch_frequencies_aggregator.h"],
    deps = [
        ":binary_content",
        ":branch_frequencies",
        ":branch_frequencies_aggregator",
        ":branch_frequencies_cc_proto",
        ":propeller_options_cc_proto",
        ":propeller_statistics",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "frequencies_branch_aggregator",
    srcs = ["frequencies_branch_aggregator.cc"],
    hdrs = ["frequencies_branch_aggregator.h"],
    deps = [
        ":bb_handle",
        ":binary_address_branch",
        ":binary_address_mapper",
        ":binary_content",
        ":branch_aggregation",
        ":branch_aggregator",
        ":branch_frequencies",
        ":branch_frequencies_aggregator",
        ":lazy_evaluator",
        ":propeller_statistics",
        ":status_macros",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/status:statusor",
        "@llvm-project//llvm:TargetParser",
    ],
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
    name = "lbr_aggregator",
    hdrs = [
        "lbr_aggregator.h",
    ],
    deps = [
        ":binary_content",
        ":lbr_aggregation",
        ":propeller_options_cc_proto",
        ":propeller_statistics",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "lbr_branch_aggregator",
    srcs = ["lbr_branch_aggregator.cc"],
    hdrs = ["lbr_branch_aggregator.h"],
    deps = [
        ":binary_address_mapper",
        ":binary_content",
        ":branch_aggregation",
        ":branch_aggregator",
        ":lazy_evaluator",
        ":lbr_aggregation",
        ":lbr_aggregator",
        ":propeller_options_cc_proto",
        ":propeller_statistics",
        ":status_macros",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "addr2cu",
    srcs = ["addr2cu.cc"],
    hdrs = ["addr2cu.h"],
    deps = [
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/log",
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
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@llvm-project//llvm:BinaryFormat",
        "@llvm-project//llvm:DebugInfoDWARF",
        "@llvm-project//llvm:Object",
        "@llvm-project//llvm:Support",
        "@llvm-project//llvm:TargetParser",
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
    deps = [
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
    ],
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
    deps = [":path_profile_options_proto"],
)

cc_proto_library(
    name = "propeller_options_cc_proto",
    deps = [":propeller_options_proto"],
)

proto_library(
    name = "path_profile_options_proto",
    srcs = ["path_profile_options.proto"],
)

cc_proto_library(
    name = "path_profile_options_cc_proto",
    deps = [":path_profile_options_proto"],
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
        "@abseil-cpp//absl/algorithm:container",
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
        ":path_node",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/functional:function_ref",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
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

cc_library(
    name = "function_chain_info",
    hdrs = ["function_chain_info.h"],
    deps = [
        ":cfg_id",
        "@abseil-cpp//absl/algorithm:container",
    ],
)

cc_library(
    name = "binary_address_branch_path",
    hdrs = ["binary_address_branch_path.h"],
    deps = [
        ":binary_address_branch",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/time",
    ],
)

cc_library(
    name = "binary_address_mapper",
    srcs = [
        "binary_address_mapper.cc",
    ],
    hdrs = ["binary_address_mapper.h"],
    deps = [
        ":bb_handle",
        ":binary_address_branch",
        ":binary_address_branch_path",
        ":binary_content",
        ":propeller_options_cc_proto",
        ":propeller_statistics",
        ":status_macros",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@llvm-project//llvm:Object",
        "@llvm-project//llvm:Support",
    ],
)

cc_library(
    name = "program_cfg_builder",
    srcs = ["program_cfg_builder.cc"],
    hdrs = ["program_cfg_builder.h"],
    deps = [
        ":addr2cu",
        ":bb_handle",
        ":binary_address_mapper",
        ":binary_content",
        ":branch_aggregation",
        ":cfg",
        ":cfg_edge",
        ":cfg_edge_kind",
        ":cfg_id",
        ":cfg_node",
        ":program_cfg",
        ":propeller_statistics",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@llvm-project//llvm:Object",
        "@llvm-project//llvm:Support",
    ],
)

cc_library(
    name = "code_layout",
    srcs = [
        "chain_cluster_builder.cc",
        "code_layout.cc",
        "code_layout_scorer.cc",
        "node_chain.cc",
        "node_chain_assembly.cc",
        "node_chain_builder.cc",
    ],
    hdrs = [
        "chain_cluster_builder.h",
        "code_layout.h",
        "code_layout_scorer.h",
        "node_chain.h",
        "node_chain_assembly.h",
        "node_chain_builder.h",
    ],
    deps = [
        ":cfg",
        ":cfg_edge",
        ":cfg_id",
        ":cfg_node",
        ":chain_merge_order",
        ":function_chain_info",
        ":program_cfg",
        ":propeller_options_cc_proto",
        ":propeller_statistics",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/functional:function_ref",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/types:span",
        "@llvm-project//llvm:Support",
    ],
)

cc_library(
    name = "perfdata_reader",
    srcs = ["perfdata_reader.cc"],
    hdrs = ["perfdata_reader.h"],
    deps = [
        ":binary_address_branch",
        ":binary_content",
        ":branch_frequencies",
        ":lbr_aggregation",
        ":perf_data_provider",
        ":spe_tid_pid_provider",
        ":status_macros",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/functional:function_ref",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/types:span",
        "@com_google_perf_data_converter//src/quipper:arm_spe_decoder",
        "@com_google_perf_data_converter//src/quipper:perf_data_cc_proto",
        "@com_google_perf_data_converter//src/quipper:perf_parser",
        "@com_google_perf_data_converter//src/quipper:perf_reader",
        "@llvm-project//llvm:Support",
    ],
)

cc_library(
    name = "perf_branch_frequencies_aggregator",
    srcs = ["perf_branch_frequencies_aggregator.cc"],
    hdrs = ["perf_branch_frequencies_aggregator.h"],
    deps = [
        ":binary_content",
        ":branch_frequencies",
        ":branch_frequencies_aggregator",
        ":perf_data_provider",
        ":perfdata_reader",
        ":propeller_options_cc_proto",
        ":propeller_statistics",
        ":resolve_mmap_name",
        ":status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "profile",
    hdrs = ["profile.h"],
    deps = [
        ":function_chain_info",
        ":program_cfg",
        ":propeller_statistics",
        "@abseil-cpp//absl/container:btree",
        "@llvm-project//llvm:Support",
    ],
)

cc_library(
    name = "profile_writer",
    srcs = ["profile_writer.cc"],
    hdrs = ["profile_writer.h"],
    deps = [
        ":cfg",
        ":cfg_edge",
        ":cfg_id",
        ":cfg_node",
        ":function_chain_info",
        ":profile",
        ":propeller_options_cc_proto",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@llvm-project//llvm:Support",
    ],
)

cc_library(
    name = "mini_disassembler",
    srcs = ["mini_disassembler.cc"],
    hdrs = ["mini_disassembler.h"],
    deps = [
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@llvm-project//llvm:AllTargetsAsmParsers",  # buildcleaner: keep
        "@llvm-project//llvm:AllTargetsDisassemblers",  # buildcleaner: keep
        "@llvm-project//llvm:MC",
        "@llvm-project//llvm:MCDisassembler",
        "@llvm-project//llvm:Object",
        "@llvm-project//llvm:Support",
        "@llvm-project//llvm:TargetParser",
    ],
)

cc_library(
    name = "perf_lbr_aggregator",
    srcs = ["perf_lbr_aggregator.cc"],
    hdrs = ["perf_lbr_aggregator.h"],
    deps = [
        ":binary_address_branch",
        ":binary_content",
        ":lbr_aggregation",
        ":lbr_aggregator",
        ":mini_disassembler",
        ":perf_data_provider",
        ":perfdata_reader",
        ":propeller_options_cc_proto",
        ":propeller_statistics",
        ":resolve_mmap_name",
        ":status_macros",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
        "@llvm-project//llvm:MC",
    ],
)

cc_library(
    name = "path_node",
    hdrs = ["path_node.h"],
    deps = [
        ":bb_handle",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/container:node_hash_map",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_library(
    name = "path_node_matchers",
    testonly = True,
    hdrs = ["path_node_matchers.h"],
    deps = [
        ":bb_handle",
        ":path_node",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@com_google_googletest//:gtest",
    ],
)

cc_library(
    name = "program_cfg_path_analyzer",
    srcs = ["program_cfg_path_analyzer.cc"],
    hdrs = ["program_cfg_path_analyzer.h"],
    deps = [
        ":bb_handle",
        ":binary_address_mapper",
        ":cfg",
        ":path_node",
        ":path_profile_options_cc_proto",
        ":program_cfg",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "path_clone_evaluator",
    srcs = ["path_clone_evaluator.cc"],
    hdrs = ["path_clone_evaluator.h"],
    deps = [
        ":bb_handle",
        ":cfg",
        ":cfg_edge_kind",
        ":cfg_id",
        ":cfg_node",
        ":code_layout",
        ":function_chain_info",
        ":path_node",
        ":path_profile_options_cc_proto",
        ":program_cfg",
        ":propeller_options_cc_proto",
        ":status_macros",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "clone_applicator",
    srcs = ["clone_applicator.cc"],
    hdrs = ["clone_applicator.h"],
    deps = [
        ":cfg",
        ":cfg_edge",
        ":cfg_edge_kind",
        ":cfg_node",
        ":code_layout",
        ":function_chain_info",
        ":path_clone_evaluator",
        ":path_node",
        ":path_profile_options_cc_proto",
        ":program_cfg",
        ":propeller_options_cc_proto",
        ":propeller_statistics",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "path_profile_aggregator",
    hdrs = ["path_profile_aggregator.h"],
    deps = [
        ":binary_address_mapper",
        ":binary_content",
        ":path_node",
        ":path_profile_options_cc_proto",
        ":program_cfg",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "perf_data_path_reader",
    srcs = ["perf_data_path_reader.cc"],
    hdrs = ["perf_data_path_reader.h"],
    deps = [
        ":binary_address_branch_path",
        ":binary_address_mapper",
        ":perfdata_reader",
        "@abseil-cpp//absl/functional:function_ref",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@com_google_perf_data_converter//src/quipper:perf_data_cc_proto",
    ],
)

cc_library(
    name = "perf_data_path_profile_aggregator",
    srcs = ["perf_data_path_profile_aggregator.cc"],
    hdrs = ["perf_data_path_profile_aggregator.h"],
    deps = [
        ":binary_address_mapper",
        ":binary_content",
        ":path_node",
        ":path_profile_aggregator",
        ":path_profile_options_cc_proto",
        ":perf_data_path_reader",
        ":perf_data_provider",
        ":perfdata_reader",
        ":program_cfg",
        ":program_cfg_path_analyzer",
        ":propeller_options_cc_proto",
        ":resolve_mmap_name",
        ":status_macros",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:vlog_is_on",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "profile_computer",
    srcs = ["profile_computer.cc"],
    hdrs = ["profile_computer.h"],
    deps = [
        ":addr2cu",
        ":binary_address_mapper",
        ":binary_content",
        ":branch_aggregation",
        ":branch_aggregator",
        ":clone_applicator",
        ":code_layout",
        ":file_perf_data_provider",
        ":function_chain_info",
        ":lbr_branch_aggregator",
        ":path_node",
        ":path_profile_aggregator",
        ":perf_data_path_profile_aggregator",
        ":perf_data_provider",
        ":perf_lbr_aggregator",
        ":profile",
        ":program_cfg",
        ":program_cfg_builder",
        ":propeller_options_cc_proto",
        ":propeller_statistics",
        ":status_macros",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
        "@llvm-project//llvm:Support",
    ],
)

cc_library(
    name = "profile_generator",
    srcs = ["profile_generator.cc"],
    hdrs = ["profile_generator.h"],
    deps = [
        ":binary_content",
        ":branch_aggregator",
        ":file_helpers",
        ":file_perf_data_provider",
        ":frequencies_branch_aggregator",
        ":lbr_branch_aggregator",
        ":path_profile_aggregator",
        ":perf_branch_frequencies_aggregator",
        ":perf_data_path_profile_aggregator",
        ":perf_data_provider",
        ":perf_lbr_aggregator",
        ":profile",
        ":profile_computer",
        ":profile_writer",
        ":propeller_options_cc_proto",
        ":proto_branch_frequencies_aggregator",
        ":status_macros",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@com_google_protobuf//:protobuf_lite",
    ],
)

cc_library(
    name = "text_proto_flag",
    hdrs = ["text_proto_flag.h"],
    deps = [
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:string_view",
        "@com_google_protobuf//:protobuf",
    ],
)

#####################
#  Binaries         #
#####################
cc_binary(
    name = "generate_propeller_profiles",
    srcs = ["generate_propeller_profiles.cc"],
    deps = [
        ":file_helpers",
        ":profile_generator",
        ":propeller_options_cc_proto",
        ":status_macros",
        ":text_proto_flag",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/flags:usage",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
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
    name = "file_helpers",
    srcs = ["file_helpers.cc"],
    hdrs = ["file_helpers.h"],
    deps = [
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
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
    name = "function_chain_info_matchers",
    testonly = True,
    srcs = ["function_chain_info_matchers.cc"],
    hdrs = ["function_chain_info_matchers.h"],
    deps = [
        ":cfg_id",
        ":function_chain_info",
        ":status_testing_macros",
        "@abseil-cpp//absl/algorithm:container",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "multi_cfg_test_case",
    testonly = True,
    srcs = ["multi_cfg_test_case.cc"],
    hdrs = ["multi_cfg_test_case.h"],
    deps = [
        ":bb_handle",
        ":cfg_edge_kind",
        ":cfg_testutil",
        ":path_node",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:node_hash_map",
        "@abseil-cpp//absl/types:span",
        "@com_google_googletest//:gtest_main",
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
        "//propeller/testdata:sample_pgo_analysis_map.bin",
    ],
    deps = [
        ":binary_content",
        ":status_testing_macros",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
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

cc_test(
    name = "binary_address_mapper_test",
    srcs = ["binary_address_mapper_test.cc"],
    data = [
        "//propeller/testdata:bimodal_sample.bin",
        "//propeller/testdata:bimodal_sample.x.bin",
        "//propeller/testdata:bimodal_sample_mfs.bin",
        "//propeller/testdata:duplicate_symbols_bin",
        "//propeller/testdata:duplicate_unique_names",
        "//propeller/testdata:sample.bin",
        "//propeller/testdata:sample_section.bin",
        "//propeller/testdata:sample_with_dropped_symbol.bin",
        "//propeller/testdata:special_case.bin",
    ],
    deps = [
        ":bb_handle",
        ":binary_address_branch_path",
        ":binary_address_mapper",
        ":binary_content",
        ":propeller_options_cc_proto",
        ":propeller_statistics",
        ":status_testing_macros",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@com_google_googletest//:gtest_main",
        "@llvm-project//llvm:Object",
        "@llvm-project//llvm:Support",
    ],
)

cc_test(
    name = "frequencies_branch_aggregator_test",
    srcs = ["frequencies_branch_aggregator_test.cc"],
    deps = [
        ":binary_address_mapper",
        ":binary_content",
        ":branch_aggregation",
        ":branch_frequencies",
        ":branch_frequencies_aggregator",
        ":frequencies_branch_aggregator",
        ":propeller_statistics",
        ":status_testing_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@com_google_googletest//:gtest_main",
        "@llvm-project//llvm:Object",
    ],
)

cc_test(
    name = "lbr_branch_aggregator_test",
    srcs = ["lbr_branch_aggregator_test.cc"],
    deps = [
        ":binary_address_mapper",
        ":binary_content",
        ":branch_aggregation",
        ":lbr_aggregation",
        ":lbr_aggregator",
        ":lbr_branch_aggregator",
        ":propeller_options_cc_proto",
        ":propeller_statistics",
        ":status_testing_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "proto_branch_frequencies_aggregator_test",
    srcs = ["proto_branch_frequencies_aggregator_test.cc"],
    deps = [
        ":binary_content",
        ":branch_frequencies",
        ":branch_frequencies_cc_proto",
        ":parse_text_proto",
        ":propeller_options_cc_proto",
        ":propeller_statistics",
        ":proto_branch_frequencies_aggregator",
        ":status_testing_macros",
        "@abseil-cpp//absl/status:status_matchers",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "code_layout_test",
    srcs = ["code_layout_test.cc"],
    data = [
        "//propeller/testdata:all_cold_landing_pads.protobuf",
        "//propeller/testdata:bimodal_sample_mfs.bin",
        "//propeller/testdata:bimodal_sample_mfs.cc_profile.txt",
        "//propeller/testdata:bimodal_sample_mfs.ld_profile.txt",
        "//propeller/testdata:call_from_simple_loop.protobuf",
        "//propeller/testdata:hot_and_cold_landing_pads.protobuf",
        "//propeller/testdata:loop_no_entry_no_exit.protobuf",
        "//propeller/testdata:multiple_cold_blocks.protobuf",
        "//propeller/testdata:nested_loop.protobuf",
        "//propeller/testdata:simple_conditionals_join.protobuf",
        "//propeller/testdata:simple_loop.protobuf",
        "//propeller/testdata:simple_multi_function.protobuf",
        "//propeller/testdata:three_branches.protobuf",
        "//propeller/testdata:two_conditionals_in_loop.protobuf",
        "//propeller/testdata:two_large_blocks.protobuf",
    ],
    deps = [
        ":cfg",
        ":cfg_edge",
        ":cfg_edge_kind",
        ":cfg_id",
        ":cfg_matchers",
        ":cfg_node",
        ":chain_merge_order",
        ":code_layout",
        ":function_chain_info",
        ":function_chain_info_matchers",
        ":mock_program_cfg_builder",
        ":program_cfg",
        ":propeller_options_cc_proto",
        ":propeller_statistics",
        ":status_testing_macros",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "perfdata_reader_test",
    size = "medium",
    timeout = "long",
    srcs = ["perfdata_reader_test.cc"],
    data = [
        "//propeller/testdata:libro_sample.so",
        "//propeller/testdata:llvm_function_samples.binary",
        "//propeller/testdata:propeller_barebone_pie_nobuildid_bin",
        "//propeller/testdata:propeller_sample_1.bin",
        "//propeller/testdata:propeller_sample_2.bin",
        "//propeller/testdata:sample.arm.bin",
        "//propeller/testdata:sample.bin",
        "//propeller/testdata:sample_different_buildid.bin",
    ],
    tags = [
        "not_build:arm",
        "not_run:arm",
    ],
    deps = [
        ":binary_address_branch",
        ":binary_content",
        ":branch_frequencies",
        ":file_perf_data_provider",
        ":lbr_aggregation",
        ":perf_data_provider",
        ":perfdata_reader",
        ":status_testing_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_google_googletest//:gtest_main",
        "@llvm-project//llvm:Object",
        "@llvm-project//llvm:Support",
    ],
)

cc_test(
    name = "perf_branch_frequencies_aggregator_test",
    srcs = ["perf_branch_frequencies_aggregator_test.cc"],
    data = [
        "//propeller/testdata:sample.arm.bin",
    ],
    deps = [
        ":binary_content",
        ":branch_frequencies",
        ":file_perf_data_provider",
        ":perf_branch_frequencies_aggregator",
        ":perf_data_provider",
        ":propeller_options_cc_proto",
        ":propeller_statistics",
        ":status_testing_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "mini_disassembler_test",
    srcs = ["mini_disassembler_test.cc"],
    data = [
        "//propeller/testdata:llvm_function_samples.binary",
    ],
    deps = [
        ":binary_content",
        ":mini_disassembler",
        ":status_testing_macros",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/strings",
        "@com_google_googletest//:gtest_main",
        "@llvm-project//llvm:MC",
        "@llvm-project//llvm:X86UtilsAndDesc",
    ],
)

cc_test(
    name = "program_cfg_path_analyzer_test",
    srcs = ["program_cfg_path_analyzer_test.cc"],
    deps = [
        ":bb_handle",
        ":binary_address_mapper",
        ":cfg_edge_kind",
        ":mock_program_cfg_builder",
        ":path_node",
        ":path_node_matchers",
        ":path_profile_options_cc_proto",
        ":program_cfg",
        ":program_cfg_path_analyzer",
        ":status_testing_macros",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "path_clone_evaluator_test",
    srcs = ["path_clone_evaluator_test.cc"],
    deps = [
        ":bb_handle",
        ":cfg",
        ":cfg_edge_kind",
        ":cfg_id",
        ":cfg_matchers",
        ":cfg_node",
        ":cfg_testutil",
        ":code_layout",
        ":function_chain_info",
        ":function_chain_info_matchers",
        ":mock_program_cfg_builder",
        ":path_clone_evaluator",
        ":path_node",
        ":path_profile_options_cc_proto",
        ":program_cfg",
        ":propeller_options_cc_proto",
        ":status_testing_macros",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:node_hash_map",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/types:span",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "clone_applicator_test",
    srcs = ["clone_applicator_test.cc"],
    deps = [
        ":cfg_edge_kind",
        ":cfg_id",
        ":cfg_matchers",
        ":cfg_testutil",
        ":clone_applicator",
        ":mock_program_cfg_builder",
        ":multi_cfg_test_case",
        ":parse_text_proto",
        ":path_clone_evaluator",
        ":path_node",
        ":program_cfg",
        ":propeller_options_cc_proto",
        ":status_testing_macros",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "profile_computer_test",
    timeout = "long",
    srcs = ["profile_computer_test.cc"],
    data = [
        "//propeller/testdata:bimodal_sample.bin",
        "//propeller/testdata:duplicate_unique_names",
        "//propeller/testdata:propeller_sample_1.bin",
        "//propeller/testdata:sample.bin",
        "//propeller/testdata:sample_O0.bin",
        "//propeller/testdata:sample_section.bin",
    ],
    shard_count = 4,  # Feel free to change this value. It's bound to drift over time.
    deps = [
        ":binary_address_mapper",
        ":binary_content",
        ":branch_aggregation",
        ":branch_aggregator",
        ":cfg",
        ":cfg_edge",
        ":cfg_id",
        ":perf_data_provider",
        ":profile",
        ":profile_computer",
        ":program_cfg",
        ":propeller_options_cc_proto",
        ":propeller_statistics",
        ":status_testing_macros",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "profile_generator_test",
    size = "medium",
    srcs = ["profile_generator_test.cc"],
    data = [
        "//propeller/testdata:bimodal_sample.bin",
        "//propeller/testdata:bimodal_sample.cloning_cc_profile.txt",
        "//propeller/testdata:bimodal_sample_mfs.bin",
        "//propeller/testdata:bimodal_sample_mfs.cc_profile.txt",
        "//propeller/testdata:bimodal_sample_mfs.cloning.cc_profile.txt",
        "//propeller/testdata:bimodal_sample_mfs.cloning.ld_profile.txt",
        "//propeller/testdata:bimodal_sample_mfs.ld_profile.txt",
        "//propeller/testdata:bimodal_sample_v2.cloning_cc_profile.txt",
        "//propeller/testdata:sample.arm.bin",
        "//propeller/testdata:sample.bin",
        "//propeller/testdata:sample_cc_directives.arm.golden.txt",
        "//propeller/testdata:sample_cc_directives.golden.txt",
        "//propeller/testdata:sample_cc_directives.v0.golden.txt",
        "//propeller/testdata:sample_ld_directives.arm.golden.txt",
        "//propeller/testdata:sample_ld_directives.golden.txt",
        "//propeller/testdata:sample_section.bin",
        "//propeller/testdata:sample_section_cc_directives.golden.txt",
        "//propeller/testdata:sample_section_ld_directives.golden.txt",
        "//propeller/testdata:sample_split_only_cc_directives.golden.txt",
        "//propeller/testdata:sample_split_only_ld_directives.golden.txt",
        "//propeller/testdata:sample_verbose_cc_directives.golden.txt",
    ],
    deps = [
        ":file_helpers",
        ":file_perf_data_provider",
        ":parse_text_proto",
        ":profile_generator",
        ":propeller_options_cc_proto",
        ":status_testing_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_google_googletest//:gtest_main",
    ],
)
