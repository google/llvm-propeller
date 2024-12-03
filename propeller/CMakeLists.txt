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

# Build all CXX targets into a unified library.
add_library(propeller_lib OBJECT
  # keep-sorted start
  addr2cu.cc
  binary_address_mapper.cc
  binary_content.cc
  branch_aggregation.cc
  branch_frequencies.cc
  cfg.cc
  cfg_edge_kind.cc
  cfg_node.cc
  chain_cluster_builder.cc
  code_layout.cc
  code_layout_scorer.cc
  file_perf_data_provider.cc
  frequencies_branch_aggregator.cc
  lbr_branch_aggregator.cc
  mini_disassembler.cc
  node_chain.cc
  node_chain_assembly.cc
  node_chain_builder.cc
  perf_branch_frequencies_aggregator.cc
  perfdata_reader.cc
  profile_writer.cc
  program_cfg.cc
  program_cfg_builder.cc
  program_cfg_path_analyzer.cc
  propeller_statistics.cc
  proto_branch_frequencies_aggregator.cc
  resolve_mmap_name.cc
  spe_tid_pid_provider.cc
  # keep-sorted end
)
target_link_libraries(propeller_lib
  # keep-sorted start
  LLVMDebugInfoDWARF
  LLVMSupport
  absl::base
  propeller_protos
  quipper_lib
  quipper_protos
  # keep-sorted end
)
foreach (tgt ${LLVM_TARGETS_TO_BUILD})
  foreach (tool AsmParser Desc Disassembler Info)
    target_link_libraries(propeller_lib LLVM${tgt}${tool})
  endforeach()
endforeach()

# Build all CXX test utilities into a unified library.
add_library(propeller_test_lib OBJECT
  # keep-sorted start
  cfg_testutil.cc
  function_chain_info_matchers.cc
  mock_program_cfg_builder.cc
  # keep-sorted end
)
target_link_libraries(propeller_test_lib
  # keep-sorted start
  absl::base
  absl::flat_hash_map
  propeller_lib
  # keep-sorted end
)

# Build all CXX tests into a unified test target.
include(${CMAKE_HOME_DIRECTORY}/CMake/GenerateTests.cmake)
propeller_generate_tests(
  SRCS
    # keep-sorted start
    branch_aggregation_test.cc
    branch_frequencies_test.cc
    cfg_test.cc
    file_perf_data_provider_test.cc
    frequencies_branch_aggregator_test.cc
    lazy_evaluator_test.cc
    lbr_branch_aggregator_test.cc
    perf_branch_frequencies_aggregator_test.cc
    perfdata_reader_test.cc
    program_cfg_path_analyzer_test.cc
    propeller_statistics_test.cc
    proto_branch_frequencies_aggregator_test.cc
    spe_tid_pid_provider_test.cc
    status_macros_test.cc
    status_testing_macros_test.cc
    # keep-sorted end
  DEPS
    # keep-sorted start
    GTest::gmock_main
    absl::base
    absl::flat_hash_set
    absl::hash
    absl::status_matchers
    propeller_lib
    propeller_test_lib
    quipper_lib
    quipper_protos
    # keep-sorted end
)