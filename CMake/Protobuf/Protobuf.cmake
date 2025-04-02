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
# Integrates protobuf into the build.

# FetchContent requires CMake 3.11.
cmake_minimum_required(VERSION 3.11)

# LINT.IfChange(version)
set(propeller_protobuf_version 30.2)
# LINT.ThenChange(../../MODULE.bazel:protobuf_version)

# Declare and configure the external protobuf package.
include(FetchContent)
FetchContent_Declare(protobuf
  GIT_REPOSITORY              "https://github.com/protocolbuffers/protobuf"
  GIT_TAG                     "v${propeller_protobuf_version}"
)

set(protobuf_ABSL_PROVIDER "package" CACHE STRING "" FORCE)
set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(protobuf_INSTALL OFF CACHE BOOL "" FORCE)
set(protobuf_WITH_ZLIB OFF CACHE BOOL "" FORCE)
set(protobuf_USE_STATIC_LIBS TRUE CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(protobuf)

if(NOT TARGET protobuf::libprotobuf OR NOT TARGET protobuf::protoc)
  message(FATAL_ERROR " Failed to fetch protobuf version ${propeller_protobuf_version}")
endif()

set(Protobuf_INCLUDE_DIR "${protobuf_SOURCE_DIR}/src" CACHE INTERNAL "")
set(Protobuf_LIBRARIES protobuf::libprotobuf CACHE INTERNAL "")

find_package(Protobuf REQUIRED)
include_directories(${Protobuf_INCLUDE_DIR})
