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

#ifndef THIRD_PARTY_PROPELLER_PROTOCOL_BUFFER_MATCHERS_H_
#define THIRD_PARTY_PROPELLER_PROTOCOL_BUFFER_MATCHERS_H_

#include "gmock/gmock.h"
#include "absl/memory/memory.h"
#include "google/protobuf/dynamic_message.h"
#include "google/protobuf/text_format.h"

// This file contains private helpers for dealing with textprotos in our
// tests.
namespace propeller_testing {

MATCHER_P(EqualsProto, textproto, "") {
  auto msg = absl::WrapUnique(arg.New());
  return google::protobuf::TextFormat::ParseFromString(textproto, msg.get()) &&
         msg->DebugString() == arg.DebugString();
}

MATCHER_P3(EqualsProtoSerialized, pool, type, textproto, "") {
  const google::protobuf::Descriptor* desc = pool->FindMessageTypeByName(type);
  google::protobuf::DynamicMessageFactory factory(pool);
  auto msg = absl::WrapUnique(factory.GetPrototype(desc)->New());
  return google::protobuf::TextFormat::ParseFromString(textproto, msg.get()) &&
         arg.SerializeAsString() == msg->SerializeAsString();
}

}  // namespace propeller_testing

#endif  // THIRD_PARTY_PROPELLER_PARSE_TEXTPROTO_H_
