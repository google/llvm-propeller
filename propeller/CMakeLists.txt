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
)
protobuf_generate(TARGET propeller_protos LANGUAGE cpp)
target_link_libraries(propeller_protos PUBLIC ${Protobuf_LIBRARIES})

# Build all CXX targets into a unified library.
add_library(propeller_lib OBJECT
  branch_aggregation.cc
  branch_frequencies.cc
)
target_link_libraries(propeller_lib
  absl::base
  propeller_protos
)

# Build all CXX tests into a unified test target.
include(${CMAKE_HOME_DIRECTORY}/CMake/GenerateTests.cmake)
propeller_generate_tests(
  SRCS
    branch_aggregation_test.cc
    branch_frequencies_test.cc
    status_macros_test.cc
  DEPS
    propeller_lib
    absl::base
    absl::hash
    absl::flat_hash_set
    absl::status_matchers
    GTest::gmock_main
)