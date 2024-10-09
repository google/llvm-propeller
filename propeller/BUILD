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
    name = "status_macros",
    hdrs = ["status_macros.h"],
    deps = [
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
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

cc_test(
    name = "status_macros_test",
    srcs = ["status_macros_test.cc"],
    deps = [
        ":status_macros",
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
        "@com_google_googletest//:gtest_main",
    ],
)
