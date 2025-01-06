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

//
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
// See the License for the specific language governing permissions and
// limitations under the License.
//
#ifndef PROPELLER_STATUS_MACROS_H_
#define PROPELLER_STATUS_MACROS_H_

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

// A set of helpers to manipulate absl::Status and absl::StatusOr.

namespace propeller_testing {
namespace internal_status {

inline const absl::Status& GetStatus(const absl::Status& status) {
  return status;
}

template <typename T>
inline const absl::Status& GetStatus(const absl::StatusOr<T> status_or) {
  return status_or.status();
}

template <typename T>
inline const absl::Status& GetStatus(
    const absl::StatusOr<std::unique_ptr<T>>& status_or) {
  return status_or.status();
}
}  // namespace internal_status
}  // namespace propeller_testing

#ifndef CONCAT_IMPL
#define CONCAT_IMPL(x, y) x##y
#endif

#ifndef CONCAT_MACRO
#define CONCAT_MACRO(x, y) CONCAT_IMPL(x, y)
#endif

#define ASSIGN_OR_RETURN(lhs, rexpr) \
  ASSIGN_OR_RETURN_IMPL(CONCAT_MACRO(_status_or, __COUNTER__), lhs, rexpr)

#define ASSIGN_OR_RETURN_IMPL(statusor, lhs, rexpr) \
  auto statusor = (rexpr);                          \
  if (!statusor.ok()) {                             \
    return statusor.status();                       \
  }                                                 \
  lhs = std::move(statusor.value())

#define RETURN_IF_ERROR(expr)                                                \
  do {                                                                       \
    const ::absl::Status _status = (expr);                                   \
    if (!_status.ok()) {                                                     \
      LOG(ERROR) << "Return Error: " << #expr << " failed with " << _status; \
      return _status;                                                        \
    }                                                                        \
  } while (0)

#define RET_CHECK_OK(status)                                                   \
  do {                                                                         \
    const ::absl::Status _status =                                             \
        ::propeller_testing::internal_status::GetStatus(status);               \
    if (!_status.ok()) {                                                       \
      LOG(ERROR) << "Return Error: " << #status << " failed with " << _status; \
      return absl::InternalError("RET_CHECK_OK fails");                        \
    }                                                                          \
  } while (0)

#define RET_CHECK_EQ(lhs, rhs)                                              \
  do {                                                                      \
    if ((lhs) != (rhs)) {                                                   \
      LOG(ERROR) << "RET_CHECK_EQ fails: lhs=" << #lhs << ", rhs=" << #rhs; \
      return absl::InternalError("RET_CHECK_EQ fails");                     \
    }                                                                       \
  } while (0)

#define RET_CHECK_LT(lhs, rhs)                                              \
  do {                                                                      \
    if (!((lhs) < (rhs))) {                                                 \
      LOG(ERROR) << "RET_CHECK_LT fails: lhs=" << #lhs << ", rhs=" << #rhs; \
      return absl::InternalError("RET_CHECK_LT fails");                     \
    }                                                                       \
  } while (0)

#endif  // PROPELLER_STATUS_MACROS_H_
