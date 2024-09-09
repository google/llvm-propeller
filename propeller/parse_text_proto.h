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

#ifndef THIRD_PARTY_PROPELLER_PARSE_TEXTPROTO_H_
#define THIRD_PARTY_PROPELLER_PARSE_TEXTPROTO_H_

#include "absl/log/absl_check.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/text_format.h"

// This file contains private helpers for dealing with textprotos in our
// tests.
namespace propeller_testing {

class ParseTextProtoOrDie {
 public:
  explicit ParseTextProtoOrDie(absl::string_view text) : text_(text) {}
  template <typename Proto>
  operator Proto() {  // NOLINT(google-explicit-constructor)
    Proto ret;
    ABSL_CHECK(google::protobuf::TextFormat::ParseFromString(text_, &ret));
    return ret;
  }

 private:
  absl::string_view text_;
};

}  // namespace propeller_testing

#endif  // THIRD_PARTY_PROPELLER_PARSE_TEXTPROTO_H_
