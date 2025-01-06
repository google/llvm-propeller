// Copyright 2024 The Propeller Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef PROPELLER_STATUS_TESTING_MACROS_H_
#define PROPELLER_STATUS_TESTING_MACROS_H_

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

// Defines useful macros for testing absl::Status and absl::StatusOr.
// TODO: b/156376166 - Remove the following fork of Status testing macros
// once they are open sourced within Abseil.

// A set of helpers to test the result of absl::Status
#define EXPECT_OK(expr) EXPECT_TRUE((expr).ok())
#define ASSERT_OK(expr) ASSERT_TRUE((expr).ok())

#ifndef CONCAT_IMPL
#define CONCAT_IMPL(x, y) x##y
#endif

#ifndef CONCAT_MACRO
#define CONCAT_MACRO(x, y) CONCAT_IMPL(x, y)
#endif

#define ASSERT_OK_AND_ASSIGN(lhs, rexpr) \
  ASSERT_OK_AND_ASSIGN_IMPL(CONCAT_MACRO(_status_or, __COUNTER__), lhs, rexpr)

#define ASSERT_OK_AND_ASSIGN_IMPL(statusor, lhs, rexpr)     \
  auto statusor = (rexpr);                                  \
  ASSERT_TRUE(statusor.status().ok()) << statusor.status(); \
  lhs = std::move(statusor.value())

#endif  // PROPELLER_STATUS_TESTING_MACROS_H_
