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

# LINT.IfChange(version)
set(_ABSL_VERSION 20240116.2)
# LINT.ThenChange(../../MODULE.bazel:abseil_version)

set(propeller_absl_build_dir ${CMAKE_BINARY_DIR}/absl-build)
set(propeller_absl_download_url https://github.com/abseil/abseil-cpp/archive/refs/tags/${_ABSL_VERSION}.zip)
set(propeller_absl_src_dir ${CMAKE_BINARY_DIR}/absl-src)

# Configure the external absl project.
configure_file(
  ${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt.in
  ${CMAKE_BINARY_DIR}/absl-external/CMakeLists.txt
)

set(ABSL_PROPAGATE_CXX_STD ON)
set(ABSL_BUILD_TESTING OFF)

# Build the external absl project.
execute_process(COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" .
  RESULT_VARIABLE result
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/absl-external )
if(result)
  message(FATAL_ERROR "CMake step for absl failed: ${result}")
endif()

execute_process(COMMAND ${CMAKE_COMMAND} --build .
  RESULT_VARIABLE result
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/absl-external)
if(result)
  message(FATAL_ERROR "Build step for absl failed: ${result}")
endif()

# Add the external absl project to the build.
add_subdirectory(${propeller_absl_src_dir} ${propeller_absl_build_dir} EXCLUDE_FROM_ALL)
