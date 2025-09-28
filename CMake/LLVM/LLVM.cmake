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
# Integrates LLVM into the build.

# LINT.IfChange(commit_hash)
set(_LLVM_HASH ebfee327df69e6cfeaa4c5300e6abd19476b8bfe)
# LINT.ThenChange(../../WORKSPACE.bzlmod:llvm_commit_hash)

set(propeller_llvm_build_dir ${CMAKE_BINARY_DIR}/llvm-build)
set(propeller_llvm_download_url https://github.com/llvm/llvm-project/archive/${_LLVM_HASH}.zip)
set(propeller_llvm_src_dir ${CMAKE_BINARY_DIR}/llvm-src)

# Configure the external llvm project.
configure_file(
  ${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt.in
  ${CMAKE_BINARY_DIR}/llvm-external/CMakeLists.txt
)

# Build the external llvm project.
execute_process(COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" .
  RESULT_VARIABLE result
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/llvm-external )
if(result)
  message(FATAL_ERROR "CMake step for llvm failed: ${result}")
endif()

### LLVM Configuration
set (LLVM_INCLUDE_UTILS OFF)
set (LLVM_INCLUDE_TESTS OFF)
set (LLVM_INCLUDE_TOOLS OFF)
set (LLVM_INCLUDE_DOCS OFF)

set (LLVM_ENABLE_RTTI ON)
set (LLVM_ENABLE_PROJECTS clang CACHE STRING
  "Semicolon-separated list of projects to build (${LLVM_KNOWN_PROJECTS}), or \"all\".")
set (LLVM_TARGETS_TO_BUILD X86 AArch64 CACHE STRING
  "Semicolon-separated list of LLVM targets to build, or \"all\".")
set (LLVM_ENABLE_ZSTD FORCE_ON)
set (LLVM_USE_STATIC_ZSTD TRUE CACHE BOOL "use static zstd")

set (LLVM_ENABLE_TERMINFO OFF CACHE BOOL "enable terminfo")

execute_process(COMMAND ${CMAKE_COMMAND} --build .
  RESULT_VARIABLE result
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/llvm-external)
if(result)
  message(FATAL_ERROR "Build step for llvm failed: ${result}")
endif()

# Add the external llvm project to the build.
add_subdirectory(${propeller_llvm_src_dir}/llvm ${propeller_llvm_build_dir}/llvm EXCLUDE_FROM_ALL)
include_directories(
  ${propeller_llvm_src_dir}
  ${propeller_llvm_src_dir}/llvm/include
  ${propeller_llvm_build_dir}
  ${propeller_llvm_build_dir}/llvm/include
)
# Add generated target-specific library directories.
foreach (tgt ${LLVM_TARGETS_TO_BUILD})
  include_directories(
    ${propeller_llvm_src_dir}/llvm/lib/Target/${tgt}
    ${propeller_llvm_build_dir}/llvm/lib/Target/${tgt}
  )
endforeach()
