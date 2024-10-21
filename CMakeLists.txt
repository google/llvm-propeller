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
# As of 2024-07-01, CMake 3.16 is the minimum supported version.
cmake_minimum_required(VERSION 3.16)

# Configure the project and global settings.
project(propeller LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
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
add_subdirectory(propeller)