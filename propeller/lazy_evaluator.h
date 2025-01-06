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

#ifndef PROPELLER_LAZY_EVALUATOR_H_
#define PROPELLER_LAZY_EVALUATOR_H_

#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include "absl/functional/any_invocable.h"
#include "absl/functional/overload.h"

namespace propeller {

// Placeholder class declaration to allow the partial specialization below.
template <typename T>
class LazyEvaluator;

// A provider object that lazily evaluates a result and caches the result.
// Example:
//   LazyEvaluator<std::string(std::vector<std::string>)> joiner(
//     {"Hello", "world!"},
//     [](std::vector<std::string> strings) -> std::string {
//         return absl::StrJoin(strings, " ");
//     });
//   std::string joined = joiner.Evaluate();
//
// For more examples, see https://godbolt.org/z/fE1bdh86v.
//
// Note: since C++ can only perform one of template deduction or implicit type
// conversion, LazyEvaluator's template types must be explicitly specified to be
// compatible with absl::AnyInvocable.
template <typename Output, typename... Inputs>
class LazyEvaluator<Output(Inputs...)> {
 public:
  // The objective of a lazy evaluator is to lazily evaluate a result, so the
  // output type must be lazily constructible.
  static_assert(!std::is_void_v<Output> &&
                std::is_move_constructible_v<Output>);

  // The inputs will be constructed at the same time as the Evaluator, so they
  // must be move-constructible in order to be passed to the adapter.
  static_assert(std::conjunction_v<std::is_move_constructible<Inputs>...>);

  // Constructs the evaluator from an output-producing adapter and any inputs.
  explicit LazyEvaluator(absl::AnyInvocable<Output(Inputs...)> adapter,
                         Inputs... inputs)
      : inputs_or_output_{InputsAndAdapter{
            .inputs = std::forward_as_tuple(std::move(inputs)...),
            .adapter = std::move(adapter)}} {}

  // Constructs the evaluator from output directly.
  explicit LazyEvaluator(Output output)
      : inputs_or_output_{std::move(output)} {}

  // Lazily evaluates the adapter, caching the results.
  const Output& Evaluate() {
    // If the output is available, return it. Otherwise, make it
    // available and return it.
    return std::visit(
        absl::Overload(
            [](const std::tuple<Output>& output) -> const Output& {
              return std::get<Output>(output);
            },
            [this](const InputsAndAdapter&) -> const Output& {
              // Extract the input.
              InputsAndAdapter inputs_and_adapter =
                  std::get<InputsAndAdapter>(std::move(inputs_or_output_));
              // Produce the output.
              inputs_or_output_.template emplace<std::tuple<Output>>(
                  std::apply(std::move(inputs_and_adapter.adapter),
                             std::move(inputs_and_adapter.inputs)));
              // Since input_or_output_ now contains an output, Evaluate() will
              // return a reference to the output.
              return Evaluate();
            }),
        inputs_or_output_);
  }

 private:
  struct InputsAndAdapter {
    std::tuple<Inputs...> inputs;
    // The adapter function that consumes the input and produces the output.
    absl::AnyInvocable<Output(Inputs...)> adapter;
  };

  std::variant<InputsAndAdapter, std::tuple<Output>> inputs_or_output_;
};
}  // namespace propeller

#endif  // PROPELLER_LAZY_EVALUATOR_H_
