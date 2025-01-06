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

#include "propeller/status_macros.h"

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "propeller/status_macros.h"
#include "propeller/status_testing_macros.h"

namespace {

using ::absl_testing::IsOk;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::testing::Test;

TEST(StatusMatchersTest, AssignOrReturn) {
  auto assigns_or_returns =
      [](const absl::StatusOr<int> &status_or) -> absl::StatusOr<int> {
    ASSIGN_OR_RETURN(int x, status_or);
    return x;
  };

  EXPECT_THAT(assigns_or_returns(absl::StatusOr<int>(1)), IsOkAndHolds(1));
  EXPECT_THAT(assigns_or_returns(absl::AbortedError("aborted")),
              StatusIs(absl::StatusCode::kAborted));
}

TEST(StatusMatchersTest, ReturnIfError) {
  auto returns_if_error =
      [](const absl::Status &status) -> absl::StatusOr<int> {
    RETURN_IF_ERROR(status);
    return 1;
  };

  EXPECT_THAT(returns_if_error(absl::OkStatus()), IsOkAndHolds(1));
  EXPECT_THAT(returns_if_error(absl::InternalError("internal error")),
              StatusIs(absl::StatusCode::kInternal));
}

TEST(StatusMatchersTest, RetCheckOk) {
  auto ret_check_ok = [](const absl::Status &status) -> absl::Status {
    RET_CHECK_OK(status);
    return absl::OkStatus();
  };

  EXPECT_THAT(ret_check_ok(absl::OkStatus()), IsOk());
  EXPECT_THAT(ret_check_ok(absl::AbortedError("aborted")),
              StatusIs(absl::StatusCode::kInternal));
}

TEST(StatusMatchersTest, RetCheckEq) {
  auto ret_check_eq = [](int lhs, int rhs) -> absl::Status {
    RET_CHECK_EQ(lhs, rhs);
    return absl::OkStatus();
  };

  EXPECT_THAT(ret_check_eq(1, 1), IsOk());
  EXPECT_THAT(ret_check_eq(1, 2), StatusIs(absl::StatusCode::kInternal));
}

TEST(StatusMatchersTest, RetCheckLt) {
  auto ret_check_lt = [](int lhs, int rhs) -> absl::Status {
    RET_CHECK_LT(lhs, rhs);
    return absl::OkStatus();
  };

  EXPECT_THAT(ret_check_lt(1, 2), IsOk());
  EXPECT_THAT(ret_check_lt(2, 1), StatusIs(absl::StatusCode::kInternal));
}

}  // namespace
