#ifndef THIRD_PARTY_LLVM_PROPELLER_TEXT_PROTO_FLAG_H
#define THIRD_PARTY_LLVM_PROPELLER_TEXT_PROTO_FLAG_H

#include <string>

#include "absl/log/check.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/text_format.h"

namespace propeller {

// A wrapper around a proto message that can be used as a flag.
template <typename Proto>
struct TextProtoFlag {
  Proto message;
};

template <typename Proto>
inline bool AbslParseFlag(absl::string_view text, TextProtoFlag<Proto>* flag,
                          std::string* err) {
  return google::protobuf::TextFormat::ParseFromString(text, &flag->message);
}

template <typename Proto>
inline std::string AbslUnparseFlag(const TextProtoFlag<Proto>& flag) {
  std::string text_proto;
  CHECK(google::protobuf::TextFormat::PrintToString(flag.message, &text_proto));
  return text_proto;
}
}  // namespace propeller

#endif  // THIRD_PARTY_LLVM_PROPELLER_TEXT_PROTO_FLAG_H
