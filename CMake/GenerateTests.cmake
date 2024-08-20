#
# Copyright 2024 The Propeller Authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

include(CMakeParseArguments)
include(GoogleTest)

# propeller_generate_tests()
#
# CMake function to generate test executables for Propeller tests.
#
# Parameters:
#   SRCS: List of the source files for the test binary.
#   DEPS: List of the dependencies for the test binary.
#
# Note: propeller_generate_tests will generate a distinct test executable for
#       each source file and add it to ctest.
#
# Usage:
# propeller_generate_tests(
#   SRCS
#     a_test.cc
#     b_test.cc
#   DEPS
#     propeller_test_utils
#     GTest::gmock
#     GTest::gtest_main
# )
function (propeller_generate_tests)
  if(NOT BUILD_TESTING)
    return()
  endif()

  cmake_parse_arguments(PROPELLER_GENERATE_TESTS
    ""
    ""
    "SRCS;DEPS"
    ${ARGN}
  )

  foreach(_TEST ${PROPELLER_GENERATE_TESTS_SRCS})
    # Create the executable and add dependencies.
    get_filename_component(TName ${_TEST} NAME_WE)
    add_executable(${TName} "")
    target_sources(${TName} PRIVATE ${_TEST})
    target_link_libraries(${TName} ${PROPELLER_GENERATE_TESTS_DEPS})

    # Add the test to ctest. If we want each test to be a separate ctest target,
    # we could instead use gtest_discover_tests(${TName}).
    add_test(NAME ${TName} COMMAND ${TName})
  endforeach()
endfunction()