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

# Build all Proto targets into a unified C++ library.
add_library(propeller_protos
  branch_frequencies.proto
  cfg.proto
  propeller_options.proto
)
protobuf_generate(TARGET propeller_protos LANGUAGE cpp)
target_link_libraries(propeller_protos PUBLIC ${Protobuf_LIBRARIES})

# Build all CXX targets into a unified library.
add_library(propeller_lib OBJECT
  addr2cu.cc
  binary_address_mapper.cc
  binary_content.cc
  branch_aggregation.cc
  branch_frequencies.cc
  cfg_edge_kind.cc
  cfg_node.cc
  cfg.cc
  chain_cluster_builder.cc
  code_layout_scorer.cc
  code_layout.cc
  file_perf_data_provider.cc
  frequencies_branch_aggregator.cc
  lbr_branch_aggregator.cc
  node_chain_assembly.cc
  node_chain_builder.cc
  node_chain.cc
  program_cfg_builder.cc
  program_cfg.cc
  propeller_statistics.cc
  proto_branch_frequencies_aggregator.cc
  resolve_mmap_name.cc
  spe_tid_pid_provider.cc
)
target_link_libraries(propeller_lib
  absl::base
  propeller_protos
  LLVMDebugInfoDWARF
  LLVMSupport
  quipper_protos
  quipper_lib
)

# Build all CXX test utilities into a unified library.
add_library(propeller_test_lib OBJECT
  cfg_testutil.cc
  function_chain_info_matchers.cc
  mock_program_cfg_builder.cc
)
target_link_libraries(propeller_test_lib
  absl::base
  absl::flat_hash_map
  propeller_lib
)

# Build all CXX tests into a unified test target.
include(${CMAKE_HOME_DIRECTORY}/CMake/GenerateTests.cmake)
propeller_generate_tests(
  SRCS
    branch_aggregation_test.cc
    branch_frequencies_test.cc
    cfg_test.cc
    file_perf_data_provider_test.cc
    frequencies_branch_aggregator_test.cc
    lazy_evaluator_test.cc
    lbr_branch_aggregator_test.cc
    propeller_statistics_test.cc
    proto_branch_frequencies_aggregator_test.cc
    spe_tid_pid_provider_test.cc
    status_macros_test.cc
    status_testing_macros_test.cc
  DEPS
    propeller_lib
    propeller_test_lib
    absl::base
    absl::hash
    absl::flat_hash_set
    absl::status_matchers
    GTest::gmock_main
    quipper_protos
    quipper_lib
)