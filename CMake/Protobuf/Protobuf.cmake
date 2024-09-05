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

set(_PROTOBUF_VERSION 27.3)

set(propeller_protobuf_build_dir ${CMAKE_BINARY_DIR}/protobuf-build)
set(propeller_protobuf_src_dir ${CMAKE_BINARY_DIR}/protobuf-src)
set (propeller_protobuf_download_url
  https://github.com/protocolbuffers/protobuf/releases/download/v${_PROTOBUF_VERSION}/protobuf-${_PROTOBUF_VERSION}.zip)

# Configure the external protobuf project.
configure_file(
  ${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt.in
  ${CMAKE_BINARY_DIR}/protobuf-external/CMakeLists.txt
)

set(protobuf_BUILD_TESTS OFF)
set(protobuf_USE_STATIC_LIBS TRUE)

# Build the external protobuf project.
execute_process(COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" .
  RESULT_VARIABLE result
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/protobuf-external )
if(result)
  message(FATAL_ERROR "CMake step for protobuf failed: ${result}")
endif()

execute_process(COMMAND ${CMAKE_COMMAND} --build .
  RESULT_VARIABLE result
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/protobuf-external)
if(result)
  message(FATAL_ERROR "Build step for protobuf failed: ${result}")
endif()

# Add the external protobuf project to the build.
add_subdirectory(${propeller_protobuf_src_dir} ${propeller_protobuf_build_dir} EXCLUDE_FROM_ALL)
include_directories(${PROTOBUF_INCLUDE_DIRS})
