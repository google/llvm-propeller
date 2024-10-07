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
#include "propeller/status_matchers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace {

using ::propeller_testing::status::IsOk;
using ::propeller_testing::status::IsOkAndHolds;
using ::propeller_testing::status::StatusIs;
using ::testing::Eq;
using ::testing::Test;

TEST(StatusMatchersTest, IsOkAndHolds) {
  absl::StatusOr<int> status_or_int = 3;
  EXPECT_THAT(status_or_int, IsOkAndHolds(Eq(3)));
}

TEST(StatusMatchersTest, StatusIs) {
  absl::StatusOr<int> status_or_int = absl::AbortedError("aborted");

  EXPECT_THAT(status_or_int, StatusIs(Eq(absl::StatusCode::kAborted)));
  EXPECT_THAT(status_or_int.status(), StatusIs(Eq(absl::StatusCode::kAborted)));
}

TEST(StatusMatchersTest, ExpectOk) { EXPECT_OK(absl::OkStatus()); }

TEST(StatusMatchersTest, AssertOk) { ASSERT_OK(absl::OkStatus()); }

TEST(StatusMatchersTest, IsOk) { ASSERT_THAT(absl::OkStatus(), IsOk()); }

TEST(StatusMatchersTest, AssertOkAndAssign) {
  ASSERT_OK_AND_ASSIGN(int x, absl::StatusOr<int>(1));
  EXPECT_EQ(x, 1);
}

}  // namespace
