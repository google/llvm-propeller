// Copyright 2026 The Propeller Authors.
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

#include "propeller/ml_code_layout.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <type_traits>
#include <vector>

#ifdef LLVM_HAVE_TFLITE
#include "llvm/Analysis/ModelUnderTrainingRunner.h"
#endif

#include "absl/log/log.h"
#include "llvm/Analysis/MLModelRunner.h"
#include "llvm/Analysis/ReleaseModeModelRunner.h"
#include "llvm/Analysis/TensorSpec.h"
#include "llvm/Analysis/Utils/TrainingLogger.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "propeller/node_chain.h"
#include "propeller/node_chain_assembly.h"

namespace propeller {
// The iterator for ML code layout features.
// The `#define F` macro must be defined before invoking this iterator.
// F is invoked with the following arguments:
// DTYPE: C++ type of the feature.
// SHAPE: Brace-enclosed initializer list for the tensor shape.
// NAME: Name of the feature.
// DESCRIPTION: Description of the feature.
// F(float, {}, reward, "The reward") \
// F(int32_t, {1}, action_step_type, "step_type")
// F(float, {1}, action_discount, "discount")
#define PROPELLER_LAYOUT_FEATURE_ITERATOR(F) \
  F(float, {1}, chain_size, "chain_size")    \
  F(float, {1}, chain_freq, "chain_freq")    \
  F(float, {1}, in_degree, "in_degree")      \
  F(float, {1}, out_degree, "out_degree")    \
  F(float, {1}, bb_count, "bb_count")        \
  F(float, {1}, is_entry, "is_entry")        \
  F(float, {}, chain_order, "The heuristic target rank")

// Enum corresponding to feature indices in PROPELLER_LAYOUT_FEATURE_ITERATOR.
enum class FeatureIndex : size_t {
#define POPULATE_INDICES(DTYPE, SHAPE, NAME, DESCRIPTION) NAME,
  PROPELLER_LAYOUT_FEATURE_ITERATOR(POPULATE_INDICES)
#undef POPULATE_INDICES
};

namespace {
#ifdef LLVM_HAVE_TFLITE
std::unique_ptr<llvm::ModelUnderTrainingRunner> model_runner_;
std::unique_ptr<llvm::ModelUnderTrainingRunner> assembly_model_runner_;
#endif
std::unique_ptr<llvm::LLVMContext> ctx_;
std::unique_ptr<llvm::Logger> logger_;
std::unique_ptr<llvm::Logger> assembly_logger_;

std::vector<float> score_gain_buckets_;
}  // namespace

const char* const kExtTSPScoreName = "target_score_gain";
const llvm::TensorSpec ExtTSPScoreSpec =
    llvm::TensorSpec::createSpec<float>(kExtTSPScoreName, {});

// Returns the feature map for the propeller layout model.
const std::vector<llvm::TensorSpec>& getFeatureMap() {
  static std::vector<llvm::TensorSpec> FeatureMap{
#define F_MAP(DTYPE, SHAPE, NAME, DESCRIPTION) \
  llvm::TensorSpec::createSpec<DTYPE>(#NAME, SHAPE),
      PROPELLER_LAYOUT_FEATURE_ITERATOR(F_MAP)
#undef F_MAP
  };
  return FeatureMap;
}

#define PROPELLER_ASSEMBLY_FEATURE_ITERATOR(F)                    \
  F(int64_t, {1}, decision_id, "decision_id")                     \
  F(int64_t, {1}, is_legal, "is_legal")                           \
  F(float, {1}, unsplit_density, "unsplit_density")               \
  F(float, {1}, split_density, "split_density")                   \
  F(int64_t, {1}, split_s1_is_entry, "s1_is_entry")               \
  F(int64_t, {1}, split_s2_is_entry, "s2_is_entry")               \
  F(int64_t, {1}, unsplit_is_entry, "u_is_entry")                 \
  F(float, {1}, ft_weight, "fallthrough_weight")                  \
  F(float, {1}, jump_thresh, "jump_threshold")                    \
  F(float, {1}, backward_jump_weight, "backward_jump_weight")     \
  F(float, {1}, backward_jump_distance, "backward_jump_distance") \
  F(int64_t, {1}, split_chain_id, "split_chain_id")               \
  F(int64_t, {1}, unsplit_chain_id, "unsplit_chain_id")           \
  F(float, {1}, unsplit_size, "unsplit_chain_size")               \
  F(float, {1}, unsplit_freq, "unsplit_chain_freq")               \
  F(float, {1}, split_size, "split_chain_size")                   \
  F(float, {1}, split_freq, "split_chain_freq")                   \
  F(float, {1}, slice_pos, "slice_pos")                           \
  F(float, {5}, merge_order, "proposed_merge_order")              \
  F(float, {1}, score_gain, "score_gain")                         \
  F(int64_t, {1}, is_chosen, "is_chosen")                         \
  F(float, {1}, edge1_weight, "weight_first_edge")                \
  F(float, {1}, edge2_weight, "weight_second_edge")               \
  F(float, {1}, edge1_distance, "distance_first_edge")            \
  F(float, {1}, edge2_distance, "distance_second_edge")           \
  F(float, {1}, broken_bond_weight, "weight_broken_bond")         \
  F(float, {1}, broken_bond_distance, "distance_broken_bond")

enum class AssemblyFeatureIndex : size_t {
#define POPULATE_INDICES(DTYPE, SHAPE, NAME, DESCRIPTION) NAME,
  PROPELLER_ASSEMBLY_FEATURE_ITERATOR(POPULATE_INDICES)
#undef POPULATE_INDICES
};

const std::vector<llvm::TensorSpec>& getAssemblyFeatureMap() {
  static std::vector<llvm::TensorSpec> FeatureMap{
#define F_MAP(DTYPE, SHAPE, NAME, DESCRIPTION) \
  llvm::TensorSpec::createSpec<DTYPE>(#NAME, SHAPE),
      PROPELLER_ASSEMBLY_FEATURE_ITERATOR(F_MAP)
#undef F_MAP
  };
  return FeatureMap;
}

#define PROPELLER_ASSEMBLY_INFERENCE_FEATURE_ITERATOR(F)                    \
  F(float, {1}, action_broken_bond_distance, "action_broken_bond_distance") \
  F(float, {1}, action_broken_bond_weight, "action_broken_bond_weight")     \
  F(int64_t, {1}, action_decision_id, "action_decision_id")                 \
  F(float, {1}, action_discount, "action_discount")                         \
  F(float, {1}, action_edge1_distance, "action_edge1_distance")             \
  F(float, {1}, action_edge1_weight, "action_edge1_weight")                 \
  F(float, {1}, action_edge2_distance, "action_edge2_distance")             \
  F(float, {1}, action_edge2_weight, "action_edge2_weight")                 \
  F(float, {1}, action_reward, "action_reward")                             \
  F(float, {1}, action_score_gain, "action_score_gain")                     \
  F(float, {1}, action_split_density, "action_split_density")               \
  F(float, {1}, action_split_freq, "action_split_freq")                     \
  F(int64_t, {1}, action_split_s1_is_entry, "action_split_s1_is_entry")     \
  F(int64_t, {1}, action_split_s2_is_entry, "action_split_s2_is_entry")     \
  F(float, {1}, action_split_size, "action_split_size")                     \
  F(int32_t, {1}, action_step_type, "action_step_type")                     \
  F(float, {1}, action_unsplit_density, "action_unsplit_density")           \
  F(float, {1}, action_unsplit_freq, "action_unsplit_freq")                 \
  F(int64_t, {1}, action_unsplit_is_entry, "action_unsplit_is_entry")       \
  F(float, {1}, action_unsplit_size, "action_unsplit_size")

enum class AssemblyInferenceFeatureIndex : size_t {
#define POPULATE_INDICES(DTYPE, SHAPE, NAME, DESCRIPTION) NAME,
  PROPELLER_ASSEMBLY_INFERENCE_FEATURE_ITERATOR(POPULATE_INDICES)
#undef POPULATE_INDICES
};

const std::vector<llvm::TensorSpec>& getAssemblyInferenceFeatureMap() {
  static std::vector<llvm::TensorSpec> FeatureMap{
#define F_MAP(DTYPE, SHAPE, NAME, DESCRIPTION) \
  llvm::TensorSpec::createSpec<DTYPE>(#NAME, SHAPE),
      PROPELLER_ASSEMBLY_INFERENCE_FEATURE_ITERATOR(F_MAP)
#undef F_MAP
  };
  return FeatureMap;
}

void LogAssemblyEvaluation(
    const propeller::PropellerCodeLayoutParameters& code_layout_params,
    const NodeChain& split_chain, const NodeChain& unsplit_chain,
    const NodeChainAssembly& assembly, bool is_chosen,
    int64_t global_decision_id) {
  static bool first_call = true;
  if (first_call) {
    first_call = false;
    std::string training_log_path = code_layout_params.training_log_path();
    if (training_log_path.empty()) {
      training_log_path = "propeller_assembly_mlgo.log";
    }

    if (ctx_ == nullptr) {
      ctx_ = std::make_unique<llvm::LLVMContext>();
    }
    std::error_code EC;
    std::unique_ptr<llvm::raw_fd_ostream> OS =
        std::make_unique<llvm::raw_fd_ostream>(training_log_path, EC);
    if (!EC) {
      assembly_logger_ = std::make_unique<llvm::Logger>(
          std::move(OS), getAssemblyFeatureMap(),
          /*RewardSpec=*/llvm::TensorSpec::createSpec<float>("reward", {1}),
          /*IncludeReward=*/false);
      assembly_logger_->switchContext("default");
    } else {
      LOG(ERROR) << "Failed to create log file: " << EC.message();
    }
  }

  if (assembly_logger_) {
    assembly_logger_->startObservation();

    float unsplit_size = static_cast<float>(unsplit_chain.size());
    float unsplit_freq = static_cast<float>(unsplit_chain.freq());
    float split_size = static_cast<float>(split_chain.size());
    float split_freq = static_cast<float>(split_chain.freq());

    float u_density = unsplit_size > 0 ? unsplit_freq / unsplit_size : 0.0f;
    float s_density = split_size > 0 ? split_freq / split_size : 0.0f;

    int64_t is_legal = assembly.is_legal() ? 1 : 0;

    float slice_pos = static_cast<float>(assembly.slice_pos().value_or(-1));

    float merge_order[5] = {0};
    int order_val = static_cast<int>(assembly.merge_order());
    if (order_val >= 0 && order_val < 5) {
      merge_order[order_val] = 1;
    }

    float score_gain = static_cast<float>(assembly.score_gain());
    int64_t chosen = is_chosen ? 1 : 0;

    int64_t split_s1_is_entry = split_chain.GetFirstNode()->is_entry() ? 1 : 0;
    int64_t split_s2_is_entry = 0;
    if (assembly.slice_pos().has_value() &&
        assembly.slice_pos().value() < split_chain.node_bundles().size()) {
      split_s2_is_entry =
          split_chain.node_bundles()[assembly.slice_pos().value()]
                  ->nodes()
                  .front()
                  ->is_entry()
              ? 1
              : 0;
    }
    int64_t unsplit_is_entry = unsplit_chain.GetFirstNode()->is_entry() ? 1 : 0;
    float ft_weight =
        static_cast<float>(code_layout_params.fallthrough_weight());
    float jump_thresh =
        static_cast<float>(code_layout_params.forward_jump_distance());
    float backward_jump_weight =
        static_cast<float>(code_layout_params.backward_jump_weight());
    float backward_jump_distance =
        static_cast<float>(code_layout_params.backward_jump_distance());

    int64_t split_chain_id =
        (static_cast<int64_t>(split_chain.id().function_index) << 32) |
        split_chain.id().intra_cfg_id.bb_index;
    int64_t unsplit_chain_id =
        (static_cast<int64_t>(unsplit_chain.id().function_index) << 32) |
        unsplit_chain.id().intra_cfg_id.bb_index;

    // Calculate edge weights and distances
    float edge1_weight = assembly.seam1_edge_weight();
    float edge2_weight = assembly.seam2_edge_weight();
    float edge1_distance = assembly.seam1_edge_distance();
    float edge2_distance = assembly.seam2_edge_distance();
    float broken_bond_weight = assembly.broken_bond_weight();
    float broken_bond_distance = assembly.broken_bond_distance();

    assembly_logger_->logTensorValue(
        static_cast<size_t>(AssemblyFeatureIndex::decision_id),
        reinterpret_cast<const char*>(&global_decision_id));
    assembly_logger_->logTensorValue(
        static_cast<size_t>(AssemblyFeatureIndex::is_legal),
        reinterpret_cast<const char*>(&is_legal));
    assembly_logger_->logTensorValue(
        static_cast<size_t>(AssemblyFeatureIndex::unsplit_density),
        reinterpret_cast<const char*>(&u_density));
    assembly_logger_->logTensorValue(
        static_cast<size_t>(AssemblyFeatureIndex::split_density),
        reinterpret_cast<const char*>(&s_density));
    assembly_logger_->logTensorValue(
        static_cast<size_t>(AssemblyFeatureIndex::split_s1_is_entry),
        reinterpret_cast<const char*>(&split_s1_is_entry));
    assembly_logger_->logTensorValue(
        static_cast<size_t>(AssemblyFeatureIndex::split_s2_is_entry),
        reinterpret_cast<const char*>(&split_s2_is_entry));
    assembly_logger_->logTensorValue(
        static_cast<size_t>(AssemblyFeatureIndex::unsplit_is_entry),
        reinterpret_cast<const char*>(&unsplit_is_entry));
    assembly_logger_->logTensorValue(
        static_cast<size_t>(AssemblyFeatureIndex::ft_weight),
        reinterpret_cast<const char*>(&ft_weight));
    assembly_logger_->logTensorValue(
        static_cast<size_t>(AssemblyFeatureIndex::jump_thresh),
        reinterpret_cast<const char*>(&jump_thresh));
    assembly_logger_->logTensorValue(
        static_cast<size_t>(AssemblyFeatureIndex::backward_jump_weight),
        reinterpret_cast<const char*>(&backward_jump_weight));
    assembly_logger_->logTensorValue(
        static_cast<size_t>(AssemblyFeatureIndex::backward_jump_distance),
        reinterpret_cast<const char*>(&backward_jump_distance));
    assembly_logger_->logTensorValue(
        static_cast<size_t>(AssemblyFeatureIndex::split_chain_id),
        reinterpret_cast<const char*>(&split_chain_id));
    assembly_logger_->logTensorValue(
        static_cast<size_t>(AssemblyFeatureIndex::unsplit_chain_id),
        reinterpret_cast<const char*>(&unsplit_chain_id));
    assembly_logger_->logTensorValue(
        static_cast<size_t>(AssemblyFeatureIndex::unsplit_size),
        reinterpret_cast<const char*>(&unsplit_size));
    assembly_logger_->logTensorValue(
        static_cast<size_t>(AssemblyFeatureIndex::unsplit_freq),
        reinterpret_cast<const char*>(&unsplit_freq));
    assembly_logger_->logTensorValue(
        static_cast<size_t>(AssemblyFeatureIndex::split_size),
        reinterpret_cast<const char*>(&split_size));
    assembly_logger_->logTensorValue(
        static_cast<size_t>(AssemblyFeatureIndex::split_freq),
        reinterpret_cast<const char*>(&split_freq));
    assembly_logger_->logTensorValue(
        static_cast<size_t>(AssemblyFeatureIndex::slice_pos),
        reinterpret_cast<const char*>(&slice_pos));
    assembly_logger_->logTensorValue(
        static_cast<size_t>(AssemblyFeatureIndex::merge_order),
        reinterpret_cast<const char*>(merge_order));
    assembly_logger_->logTensorValue(
        static_cast<size_t>(AssemblyFeatureIndex::score_gain),
        reinterpret_cast<const char*>(&score_gain));
    assembly_logger_->logTensorValue(
        static_cast<size_t>(AssemblyFeatureIndex::is_chosen),
        reinterpret_cast<const char*>(&chosen));
    assembly_logger_->logTensorValue(
        static_cast<size_t>(AssemblyFeatureIndex::edge1_weight),
        reinterpret_cast<const char*>(&edge1_weight));
    assembly_logger_->logTensorValue(
        static_cast<size_t>(AssemblyFeatureIndex::edge2_weight),
        reinterpret_cast<const char*>(&edge2_weight));
    assembly_logger_->logTensorValue(
        static_cast<size_t>(AssemblyFeatureIndex::edge1_distance),
        reinterpret_cast<const char*>(&edge1_distance));
    assembly_logger_->logTensorValue(
        static_cast<size_t>(AssemblyFeatureIndex::edge2_distance),
        reinterpret_cast<const char*>(&edge2_distance));
    assembly_logger_->logTensorValue(
        static_cast<size_t>(AssemblyFeatureIndex::broken_bond_weight),
        reinterpret_cast<const char*>(&broken_bond_weight));
    assembly_logger_->logTensorValue(
        static_cast<size_t>(AssemblyFeatureIndex::broken_bond_distance),
        reinterpret_cast<const char*>(&broken_bond_distance));

    assembly_logger_->endObservation();
  }
}

#ifdef LLVM_HAVE_TFLITE
static int log_counter = 0;
#endif

double GetAssemblyScoreML(
    const propeller::PropellerCodeLayoutParameters& code_layout_params,
    const NodeChain& split_chain, const NodeChain& unsplit_chain,
    const NodeChainAssembly& assembly, bool is_chosen,
    int64_t global_decision_id) {
#ifdef LLVM_HAVE_TFLITE
  if (!model_runner_) {
    if (ctx_ == nullptr) {
      LOG(INFO)
          << "DEBUG: GetEdgeScoreML is being called, model_runner_ is null, "
             "ctx_ is null.";
      ctx_ = std::make_unique<llvm::LLVMContext>();
    }

    const std::string& policy_path = code_layout_params.policy_path();

    if (!policy_path.empty()) {
      LOG(INFO) << "DEBUG: policy_path: " << policy_path;
      llvm::SmallString<256> model_path(policy_path);
      llvm::SmallString<256> spec_path(policy_path);
      llvm::sys::path::append(spec_path, "output_spec.json");
      model_runner_ = llvm::ModelUnderTrainingRunner::createAndEnsureValid(
          *ctx_, std::string(model_path.str()),
          kExtTSPScoreName,  // The expected output tensor name
          getAssemblyInferenceFeatureMap(), spec_path);
      if (model_runner_) {
        LOG(INFO) << "DEBUG: Model runner created successfully.";
      } else {
        LOG(INFO) << "DEBUG: Failed to create model runner.";
      }
    }
  }

  if (model_runner_) {
    float unsplit_size = static_cast<float>(unsplit_chain.size());
    float unsplit_freq = static_cast<float>(unsplit_chain.freq());
    float split_size = static_cast<float>(split_chain.size());
    float split_freq = static_cast<float>(split_chain.freq());

    float u_density = unsplit_size > 0 ? unsplit_freq / unsplit_size : 0.0f;
    float s_density = split_size > 0 ? split_freq / split_size : 0.0f;

    int64_t split_s1_is_entry = split_chain.GetFirstNode()->is_entry() ? 1 : 0;
    int64_t split_s2_is_entry = 0;
    if (assembly.slice_pos().has_value() &&
        assembly.slice_pos().value() < split_chain.node_bundles().size()) {
      split_s2_is_entry =
          split_chain.node_bundles()[assembly.slice_pos().value()]
                  ->nodes()
                  .front()
                  ->is_entry()
              ? 1
              : 0;
    }
    int64_t unsplit_is_entry = unsplit_chain.GetFirstNode()->is_entry() ? 1 : 0;

    // Calculate edge weights and distances
    float edge1_weight = assembly.seam1_edge_weight();
    float edge2_weight = assembly.seam2_edge_weight();
    float edge1_distance = assembly.seam1_edge_distance();
    float edge2_distance = assembly.seam2_edge_distance();
    float broken_bond_weight = assembly.broken_bond_weight();
    float broken_bond_distance = assembly.broken_bond_distance();

    // *model_runner_->getTensor<int64_t>(
    //     static_cast<int>(AssemblyInferenceFeatureIndex::action_decision_id))
    //     = global_decision_id;

    *model_runner_->getTensor<float>(static_cast<int>(
        AssemblyInferenceFeatureIndex::action_unsplit_density)) = u_density;

    *model_runner_->getTensor<float>(static_cast<int>(
        AssemblyInferenceFeatureIndex::action_split_density)) = s_density;

    *model_runner_->getTensor<int64_t>(static_cast<int>(
        AssemblyInferenceFeatureIndex::action_split_s1_is_entry)) =
        split_s1_is_entry;

    *model_runner_->getTensor<int64_t>(static_cast<int>(
        AssemblyInferenceFeatureIndex::action_split_s2_is_entry)) =
        split_s2_is_entry;

    *model_runner_->getTensor<int64_t>(static_cast<int>(
        AssemblyInferenceFeatureIndex::action_unsplit_is_entry)) =
        unsplit_is_entry;

    *model_runner_->getTensor<float>(static_cast<int>(
        AssemblyInferenceFeatureIndex::action_unsplit_size)) = unsplit_size;
    *model_runner_->getTensor<float>(static_cast<int>(
        AssemblyInferenceFeatureIndex::action_unsplit_freq)) = unsplit_freq;
    *model_runner_->getTensor<float>(static_cast<int>(
        AssemblyInferenceFeatureIndex::action_split_size)) = split_size;
    *model_runner_->getTensor<float>(static_cast<int>(
        AssemblyInferenceFeatureIndex::action_split_freq)) = split_freq;

    *model_runner_->getTensor<float>(static_cast<int>(
        AssemblyInferenceFeatureIndex::action_edge1_weight)) = edge1_weight;
    *model_runner_->getTensor<float>(static_cast<int>(
        AssemblyInferenceFeatureIndex::action_edge2_weight)) = edge2_weight;
    *model_runner_->getTensor<float>(static_cast<int>(
        AssemblyInferenceFeatureIndex::action_edge1_distance)) = edge1_distance;
    *model_runner_->getTensor<float>(static_cast<int>(
        AssemblyInferenceFeatureIndex::action_edge2_distance)) = edge2_distance;
    *model_runner_->getTensor<float>(static_cast<int>(
        AssemblyInferenceFeatureIndex::action_broken_bond_weight)) =
        broken_bond_weight;
    *model_runner_->getTensor<float>(static_cast<int>(
        AssemblyInferenceFeatureIndex::action_broken_bond_distance)) =
        broken_bond_distance;

    float score = model_runner_->evaluate<float>();

    static const std::vector<float> score_gain_buckets =
        [&code_layout_params]() {
          std::vector<float> buckets;
          llvm::SmallString<256> buckets_path(code_layout_params.policy_path());
          llvm::sys::path::append(buckets_path, "score_gain.buckets");
          std::ifstream file(std::string(buckets_path.str()));
          std::string line;
          while (std::getline(file, line)) {
            if (!line.empty()) {
              buckets.push_back(std::stof(line));
            }
          }
          if (buckets.empty()) buckets.push_back(0.0f);  // Fallback safe value
          return buckets;
        }();

    int index = std::clamp(static_cast<int>(score * 1000), 0,
                           static_cast<int>(score_gain_buckets.size() - 1));
    float mapped_score = score_gain_buckets[index];

    float score_gain = static_cast<float>(assembly.score_gain());

    if (log_counter++ < 10) {
      LOG(INFO) << "DEBUG: ML raw score: " << score
                << ", mapped bucket index: " << index
                << ", mapped_score: " << mapped_score
                << ", propeller_score: " << score_gain << "\n";
    }

    return mapped_score;
  }

#endif

  return 0.0;
}

}  // namespace propeller
