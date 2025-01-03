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

# https://github.com/google/oss-policies-info/blob/main/foundational-cxx-support-matrix.md
# As of 2025-01-03, CMake 3.24 is the minimum supported version.
cmake_minimum_required(VERSION 3.24)

# Configure the project and global settings.
project(propeller LANGUAGES CXX)
# LINT.IfChange(cpp_standard)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# LINT.ThenChange(.bazelrc:cpp_standard)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
endif()

# Set up system-level dependencies.
find_library (LIBELF_LIBRARIES NAMES elf REQUIRED)
find_library (LIBZ_LIBRARIES NAMES z REQUIRED)
find_library (LIBCRYPTO_LIBRARIES NAMES crypto REQUIRED)

# Set up external dependencies.
include(CTest)
include(CMake/Googletest/Googletest.cmake)
# Absl's testing helpers depend on Googletest and CTest, so they must be
# imported first.
include(CMake/Absl/Absl.cmake)
include(CMake/LLVM/LLVM.cmake)
include(CMake/Protobuf/Protobuf.cmake)
include(CMake/Quipper/Quipper.cmake)

include_directories(${CMAKE_HOME_DIRECTORY} ${PROJECT_BINARY_DIR})

# Generate protos at the root level so that protoc uses the right import paths.
file(
    GLOB_RECURSE propeller_proto_sources
    RELATIVE ${CMAKE_HOME_DIRECTORY}
    propeller/*.proto
)
# Build all Propeller proto targets into a unified C++ library.
add_library(propeller_protos ${propeller_proto_sources})
protobuf_generate(TARGET propeller_protos LANGUAGE cpp)
target_link_libraries(propeller_protos PUBLIC ${Protobuf_LIBRARIES})

add_subdirectory(propeller)