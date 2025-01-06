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

// Copyright 2024 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// see the license for the specific language governing permissions and
// limitations under the license.
#include "propeller/status_testing_macros.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest-spi.h"
#include "gtest/gtest.h"

namespace {

TEST(StatusTestingMacrosTest, ExpectOkHandlesOkStatus) {
  EXPECT_OK(absl::OkStatus());
}

TEST(StatusTestingMacrosTest, ExpectOkNonfatallyFailsOnStatus) {
  EXPECT_NONFATAL_FAILURE(EXPECT_OK(absl::InternalError("Internal error")), "");
}

TEST(StatusTestingMacrosTest, AssertOkHandlesOkStatus) {
  ASSERT_OK(absl::OkStatus());
}

TEST(StatusTestingMacrosTest, AssertOkFatallyFailsOnStatus) {
  EXPECT_FATAL_FAILURE(ASSERT_OK(absl::InternalError("Internal error")), "");
}

TEST(StatusTestingMacrosTest, AssertOkAndAssignHandlesOkStatus) {
  ASSERT_OK_AND_ASSIGN(int x, absl::StatusOr<int>(1));
  EXPECT_EQ(x, 1);
}

TEST(StatusTestingMacrosTest, AssertOkAndAssignFatallyFailsOnStatus) {
  EXPECT_FATAL_FAILURE(
      ASSERT_OK_AND_ASSIGN(
          int _, absl::StatusOr<int>(absl::InternalError("Internal error"))),
      "");
}

}  // namespace
