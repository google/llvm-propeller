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
# Integrates googletest into the build.

# LINT.IfChange(version)
set(_GTEST_VERSION 1.15.0)
# LINT.ThenChange(../../MODULE.bazel:gtest_version)

set(propeller_gtest_build_dir ${CMAKE_BINARY_DIR}/googletest-build)
set(propeller_gtest_download_url https://github.com/google/googletest/archive/refs/tags/v${_GTEST_VERSION}.zip)
set(propeller_gtest_src_dir ${CMAKE_BINARY_DIR}/googletest-src)

# Configure the external googletest project.
configure_file(
  ${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt.in
  ${CMAKE_BINARY_DIR}/googletest-external/CMakeLists.txt
)

# Build the external googletest project.
execute_process(COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" .
  RESULT_VARIABLE result
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/googletest-external )
if(result)
  message(FATAL_ERROR "CMake step for googletest failed: ${result}")
endif()

execute_process(COMMAND ${CMAKE_COMMAND} --build .
  RESULT_VARIABLE result
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/googletest-external)
if(result)
  message(FATAL_ERROR "Build step for googletest failed: ${result}")
endif()

# Add the external googletest project to the build.
add_subdirectory(${propeller_gtest_src_dir} ${propeller_gtest_build_dir} EXCLUDE_FROM_ALL)
include_directories(${propeller_gtest_src_dir}/googletest/include ${propeller_gtest_src_dir}/googlemock/include)
