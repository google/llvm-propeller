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
# Integrates Perf Data Converter / Quipper into the build.

# LINT.IfChange(commit_hash)
set(_QUIPPER_HASH f76cd4dd1e85bb54d60ea3fe69f92168fdf94edb)
# LINT.ThenChange(../../MODULE.bazel:quipper_version)

set(propeller_quipper_build_dir ${CMAKE_BINARY_DIR}/quipper-build)
set(propeller_quipper_download_url https://github.com/google/perf_data_converter/archive/${_QUIPPER_HASH}.tar.gz)
set(propeller_quipper_src_dir ${CMAKE_BINARY_DIR}/quipper-src)

# Configure the external Quipper project.
configure_file(
  ${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt.in
  ${CMAKE_BINARY_DIR}/quipper-external/CMakeLists.txt
)

# Build the external Quipper project.
execute_process(COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" .
  RESULT_VARIABLE result
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/quipper-external )
if(result)
  message(FATAL_ERROR "CMake step for Quipper failed: ${result}")
endif()

execute_process(COMMAND ${CMAKE_COMMAND} --build .
  RESULT_VARIABLE result
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/quipper-external)
if(result)
  message(FATAL_ERROR "Build step for quipper failed: ${result}")
endif()

# Add the external Quipper directory to the build.
include_directories(${propeller_quipper_src_dir}/src)
