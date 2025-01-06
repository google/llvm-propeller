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

#include "propeller/code_layout.h"

#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "propeller/cfg.h"
#include "propeller/cfg_edge.h"
#include "propeller/cfg_edge_kind.h"
#include "propeller/cfg_id.h"
#include "propeller/cfg_matchers.h"
#include "propeller/cfg_node.h"
#include "propeller/chain_cluster_builder.h"
#include "propeller/chain_merge_order.h"
#include "propeller/code_layout_scorer.h"
#include "propeller/function_chain_info.h"
#include "propeller/function_chain_info_matchers.h"
#include "propeller/mock_program_cfg_builder.h"
#include "propeller/node_chain.h"
#include "propeller/node_chain_assembly.h"
#include "propeller/node_chain_builder.h"
#include "propeller/program_cfg.h"
#include "propeller/propeller_options.pb.h"
#include "propeller/propeller_statistics.h"
#include "propeller/status_testing_macros.h"

namespace propeller {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::testing::_;
using ::testing::Contains;
using ::testing::DescribeMatcher;
using ::testing::DoubleNear;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Key;
using ::testing::Matcher;
using ::testing::Pair;
using ::testing::Pointee;
using ::testing::Property;
using ::testing::ResultOf;
using ::testing::SizeIs;
using ::testing::UnorderedElementsAre;

// Epsilon used to avoid double precision problem.
constexpr double kEpsilon = 0.001;

MATCHER_P(ChainIdIs, chain_id, "") { return arg->id() == chain_id; }

MATCHER_P(HasIntraChainEdges, intra_chain_out_edges_matcher,
          absl::StrCat(negation ? "doesn't have" : "has",
                       " intra_chain_out_edges_ that ",
                       DescribeMatcher<std::vector<CFGEdge *>>(
                           intra_chain_out_edges_matcher, negation))) {
  return ExplainMatchResult(intra_chain_out_edges_matcher,
                            arg.intra_chain_out_edges(), result_listener);
}

std::string GetTestInputPath(absl::string_view testdata_path) {
  return absl::StrCat(::testing::SrcDir(), testdata_path);
}

// Helper method to capture the node ordinals in a chain/cluster and place them
// in a vector.
template <class Container>
std::vector<InterCfgId> GetOrderedNodeIds(const Container &container) {
  std::vector<InterCfgId> node_ids;
  container.VisitEachNodeRef(
      [&](const CFGNode &n) { node_ids.push_back(n.inter_cfg_id()); });
  return node_ids;
}

std::vector<InterCfgId> GetOrderedNodeIds(const NodeChainAssembly &assembly) {
  std::vector<InterCfgId> node_ids;
  assembly.VisitEachNodeBundleInAssemblyOrder(
      [&node_ids](const CFGNodeBundle &bundle) {
        for (const CFGNode *node : bundle.nodes())
          node_ids.push_back(node->inter_cfg_id());
      });
  return node_ids;
}

// Captures the nodes of a cfg keyed by their id.
absl::flat_hash_map<InterCfgId, CFGNode *> GetCfgNodes(
    const ControlFlowGraph &cfg) {
  absl::flat_hash_map<InterCfgId, CFGNode *> nodes_by_id_;
  for (const std::unique_ptr<CFGNode> &node_ptr : cfg.nodes())
    nodes_by_id_.emplace(node_ptr->inter_cfg_id(), node_ptr.get());
  return nodes_by_id_;
}

// Creates one chain containing the given nodes.
NodeChain CreateNodeChain(absl::Span<CFGNode *const> nodes) {
  CHECK(!nodes.empty());
  NodeChain chain({{nodes.front()}});
  for (int i = 1; i < nodes.size(); ++i) {
    NodeChain other_chain({{nodes[i]}});
    chain.MergeWith(other_chain);
  }
  return chain;
}

// Given a NodeChainAssembly and a CFG, returns the slice indices of all the
// CFG nodes in that assembly. The return value is a map keyed by ordinals of
// the CFG nodes mapped to their slice index in the assembly (or std::nullopt)
// if they don't occur in the assembly.
absl::flat_hash_map<InterCfgId, std::optional<int>> GetSliceIndices(
    const NodeToBundleMapper &node_to_bundle_mapper,
    const NodeChainAssembly &assembly, const ControlFlowGraph &cfg) {
  absl::flat_hash_map<InterCfgId, std::optional<int>> slice_index_map;
  for (const std::unique_ptr<CFGNode> &node : cfg.nodes()) {
    const NodeToBundleMapper::BundleMappingEntry &bundle_mapping =
        node_to_bundle_mapper.GetBundleMappingEntry(node.get());
    slice_index_map.emplace(
        node->inter_cfg_id(),
        assembly.FindSliceIndex(node.get(), bundle_mapping));
  }
  return slice_index_map;
}

// Returns a NodeChainBuilder for CFGs with names in `cfg_names` found in
// `program_cfg`. `stats` must outlive the returned NodeChainBuilder.
// This will use the default `NodeChainAssemblyQueue` implementation for
// `node_chain_assemblies_`. Use this when the code under test does not depend
// on `NodeChainAssemblyQueue`. Otherwise, use the type-parameterized
// `NodeChainBuilderTest` to verify that the test works for every
// implementation.
NodeChainBuilder CreateNodeChainBuilderForCfgs(
    const ProgramCfg &program_cfg, absl::Span<const int> function_indices,
    const PropellerCodeLayoutParameters &code_layout_params,
    PropellerStats::CodeLayoutStats &stats) {
  const PropellerCodeLayoutScorer scorer(code_layout_params);
  std::vector<const ControlFlowGraph *> cfgs;
  for (int function_index : function_indices) {
    cfgs.push_back(program_cfg.GetCfgByIndex(function_index));
  }
  return NodeChainBuilder::CreateNodeChainBuilder(scorer, cfgs,
                                                  /*initial_chains=*/{}, stats);
}

// Given a vector of 2D vectors of BB ids `chains`, constructs and returns a
// vector of `FunctionChainInfo::BbChain`s.
std::vector<FunctionChainInfo::BbChain> ConstructBbChains(
    absl::Span<const absl::Span<const absl::Span<const IntraCfgId>>> chains) {
  std::vector<FunctionChainInfo::BbChain> bb_chains;
  absl::c_transform(chains, std::back_inserter(bb_chains),
                    [](absl::Span<const absl::Span<const IntraCfgId>> chain) {
                      FunctionChainInfo::BbChain bb_chain(/*_layout_index=*/0);
                      absl::c_transform(
                          chain, std::back_inserter(bb_chain.bb_bundles),
                          [](absl::Span<const IntraCfgId> bb_ids) {
                            FunctionChainInfo::BbBundle bb_bundle;
                            absl::c_transform(
                                bb_ids,
                                std::back_inserter(bb_bundle.full_bb_ids),
                                [](const IntraCfgId &bb_id) {
                                  return FullIntraCfgId{.intra_cfg_id = bb_id};
                                });
                            return bb_bundle;
                          });
                      return bb_chain;
                    });
  return bb_chains;
}

// Test the proper construction of NodeChainSlice
TEST(NodeChainSliceTest, TestCreateNodeChainSlice) {
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ProtoProgramCfg> proto_program_cfg,
      BuildFromCfgProtoPath(GetTestInputPath("_main/propeller/testdata/"
                                             "three_branches.protobuf")));
  const ControlFlowGraph &foo_cfg =
      *proto_program_cfg->program_cfg().GetCfgByIndex(0);
  EXPECT_EQ(foo_cfg.GetPrimaryName(), "foo");
  absl::flat_hash_map<InterCfgId, CFGNode *> foo_nodes = GetCfgNodes(foo_cfg);
  NodeChain chain =
      CreateNodeChain({foo_nodes.at({0, {0, 0}}), foo_nodes.at({0, {1, 0}}),
                       foo_nodes.at({0, {2, 0}})});
  NodeChainSlice chain_slice1(chain, 0, 2);
  EXPECT_EQ(chain_slice1.begin_offset(), 0);
  EXPECT_EQ(chain_slice1.end_offset(), foo_nodes.at({0, {0, 0}})->size() +
                                           foo_nodes.at({0, {1, 0}})->size());
  NodeChainSlice chain_slice2(chain, 1, 3);
  EXPECT_EQ(chain_slice2.begin_offset(), foo_nodes.at({0, {0, 0}})->size());
  EXPECT_EQ(chain_slice2.end_offset(), chain.size());
  EXPECT_EQ(chain_slice2.size(), foo_nodes.at({0, {1, 0}})->size() +
                                     foo_nodes.at({0, {2, 0}})->size());
  EXPECT_EQ(chain_slice2.end_pos(), chain.node_bundles().end());
  EXPECT_EQ(chain_slice2.begin_pos(), chain.node_bundles().begin() + 1);
  EXPECT_DEATH(NodeChainSlice(chain, 2, 1), HasSubstr("begin <= end"));
  EXPECT_DEATH(NodeChainSlice(chain, 4, 5),
               HasSubstr("begin <= chain.node_bundles().size()"));
}

TEST(CodeLayoutScorerTest, GetEdgeScore) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ProtoProgramCfg> proto_program_cfg,
                       BuildFromCfgProtoPath(
                           GetTestInputPath("_main/propeller/testdata/"
                                            "simple_multi_function.protobuf")));
  const ControlFlowGraph &foo_cfg =
      *proto_program_cfg->program_cfg().GetCfgByIndex(0);
  const ControlFlowGraph &bar_cfg =
      *proto_program_cfg->program_cfg().GetCfgByIndex(1);

  // Build a layout scorer with specific parameters.
  PropellerCodeLayoutParameters params;
  params.set_fallthrough_weight(10);
  params.set_forward_jump_weight(2);
  params.set_backward_jump_weight(1);
  params.set_forward_jump_distance(200);
  params.set_backward_jump_distance(100);
  PropellerCodeLayoutScorer scorer(params);

  ASSERT_THAT(bar_cfg.inter_edges(), SizeIs(1));
  {
    const auto &call_edge = bar_cfg.inter_edges().front();
    ASSERT_TRUE(call_edge->IsCall());
    ASSERT_NE(call_edge->weight(), 0);
    ASSERT_NE(call_edge->src()->size(), 0);
    // Score with negative src-to-sink distance (backward call).
    // Check that for calls, half of src size is always added to the distance.
    EXPECT_EQ(scorer.GetEdgeScore(*call_edge, -10),
              call_edge->weight() * 1 *
                  (1.0 - (10 - call_edge->src()->size() / 2) / 100.0));
    // Score with zero src-to-sink distance (forward call).
    EXPECT_EQ(scorer.GetEdgeScore(*call_edge, 0),
              call_edge->weight() * 2 *
                  (1.0 - (call_edge->src()->size() / 2) / 200.0));
    // Score with positive src-to-sink distance (forward call).
    EXPECT_EQ(scorer.GetEdgeScore(*call_edge, 20),
              call_edge->weight() * 2 *
                  (1.0 - (20 + call_edge->src()->size() / 2) / 200.0));
    // Score must be zero when beyond the src-to-sink distance exceeds the
    // distance parameters.
    EXPECT_EQ(scorer.GetEdgeScore(*call_edge, 250), 0);
    EXPECT_EQ(scorer.GetEdgeScore(*call_edge, -150), 0);
  }

  ASSERT_THAT(foo_cfg.inter_edges(), SizeIs(2));
  for (const std::unique_ptr<CFGEdge> &ret_edge : foo_cfg.inter_edges()) {
    ASSERT_TRUE(ret_edge->IsReturn());
    ASSERT_NE(ret_edge->weight(), 0);
    ASSERT_NE(ret_edge->sink()->size(), 0);
    // Score with negative src-to-sink distance (backward return).
    // Check that for returns, half of sink size is always added to the
    // distance.
    EXPECT_EQ(scorer.GetEdgeScore(*ret_edge, -10),
              ret_edge->weight() * 1 *
                  (1.0 - (10 - ret_edge->sink()->size() / 2) / 100.0));
    // Score with zero src-to-sink distance (forward return).
    EXPECT_EQ(scorer.GetEdgeScore(*ret_edge, 0),
              ret_edge->weight() * 2 *
                  (1.0 - (ret_edge->sink()->size() / 2) / 200.0));
    // Score with positive src-to-sink distance (forward return).
    EXPECT_EQ(scorer.GetEdgeScore(*ret_edge, 20),
              ret_edge->weight() * 2 *
                  (1.0 - (20 + ret_edge->sink()->size() / 2) / 200.0));
    EXPECT_EQ(scorer.GetEdgeScore(*ret_edge, 250), 0);
    EXPECT_EQ(scorer.GetEdgeScore(*ret_edge, -150), 0);
  }

  for (const std::unique_ptr<CFGEdge> &edge : foo_cfg.intra_edges()) {
    ASSERT_EQ(edge->kind(), CFGEdgeKind::kBranchOrFallthough);
    ASSERT_NE(edge->weight(), 0);
    // Fallthrough score.
    EXPECT_EQ(scorer.GetEdgeScore(*edge, 0), edge->weight() * 10);
    // Backward edge (within distance threshold) score.
    EXPECT_EQ(scorer.GetEdgeScore(*edge, -40),
              edge->weight() * 1 * (1.0 - 40 / 100.0));
    // Forward edge (within distance threshold) score.
    EXPECT_EQ(scorer.GetEdgeScore(*edge, 80),
              edge->weight() * 2 * (1.0 - 80 / 200.0));
    // Forward and backward edge beyond the distance thresholds (zero score).
    EXPECT_EQ(scorer.GetEdgeScore(*edge, 201), 0);
    EXPECT_EQ(scorer.GetEdgeScore(*edge, -101), 0);
  }
}

// Type-parameterized test fixture for `NodeChainBuilder` tests. This allows
// testing `NodeChainBuilder` with both `NodeChainAssemblyIterativeQueue` and
// and `NodeChainAssemblyBalancedTreeQueue` implementations.
template <typename NodeChainAssemblyQueueImpl>
class NodeChainBuilderTest : public testing::Test {
 protected:
  // Returns a `NodeChainBuilder` for CFGs with function_indexes in
  // `function_indices` found in `program_cfg`. `stats` must outlive the
  // returned NodeChainBuilder.
  static NodeChainBuilder InitializeNodeChainBuilderForCfgs(
      const ProgramCfg &program_cfg, absl::Span<const int> function_indices,
      const PropellerCodeLayoutParameters &code_layout_params,
      PropellerStats::CodeLayoutStats &stats) {
    std::vector<const ControlFlowGraph *> cfgs;
    for (int function_index : function_indices) {
      cfgs.push_back(program_cfg.GetCfgByIndex(function_index));
    }
    return NodeChainBuilder::CreateNodeChainBuilder<NodeChainAssemblyQueueImpl>(
        PropellerCodeLayoutScorer(code_layout_params), cfgs,
        /*initial_chains=*/{}, stats);
  }
};

using NodeChainAssemblyQueueTypes =
    testing::Types<NodeChainAssemblyIterativeQueue,
                   NodeChainAssemblyBalancedTreeQueue>;
TYPED_TEST_SUITE(NodeChainBuilderTest, NodeChainAssemblyQueueTypes);

// Check that MergeChain(NodeChain&, NodeChain&) properly updates the chain
// edges by calling MergeChainEdges.
TYPED_TEST(NodeChainBuilderTest, MergeChainsUpdatesChainEdges) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ProtoProgramCfg> proto_program_cfg,
                       BuildFromCfgProtoPath(GetTestInputPath(
                           "_main/propeller/testdata/"
                           "simple_conditionals_join.protobuf")));
  ASSERT_THAT(proto_program_cfg->program_cfg().cfgs_by_index(),
              UnorderedElementsAre(Key(10)));
  PropellerStats::CodeLayoutStats stats;
  NodeChainBuilder chain_builder = this->InitializeNodeChainBuilderForCfgs(
      proto_program_cfg->program_cfg(), /*function_indices=*/{10},
      PropellerCodeLayoutParameters(), stats);
  chain_builder.InitNodeChains();
  chain_builder.InitChainEdges();
  const absl::flat_hash_map<InterCfgId, std::unique_ptr<NodeChain>> &chains =
      chain_builder.chains();

  EXPECT_THAT(
      chains.at({10, {0, 0}})->inter_chain_out_edges(),
      UnorderedElementsAre(Pair(chains.at({10, {1, 0}}).get(),
                                ElementsAre(Pointee(IsCfgEdge(
                                    NodeIndexIs(0), NodeIndexIs(1), 110,
                                    CFGEdgeKind::kBranchOrFallthough)))),
                           Pair(chains.at({10, {2, 0}}).get(),
                                ElementsAre(Pointee(IsCfgEdge(
                                    NodeIndexIs(0), NodeIndexIs(2), 150,
                                    CFGEdgeKind::kBranchOrFallthough))))));
  EXPECT_THAT(chains.at({10, {0, 0}})->inter_chain_in_edges(), IsEmpty());
  EXPECT_THAT(
      chains.at({10, {1, 0}})->inter_chain_out_edges(),
      UnorderedElementsAre(Pair(chains.at({10, {2, 0}}).get(),
                                ElementsAre(Pointee(IsCfgEdge(
                                    NodeIndexIs(1), NodeIndexIs(2), 100,
                                    CFGEdgeKind::kBranchOrFallthough)))),
                           Pair(chains.at({10, {3, 0}}).get(),
                                ElementsAre(Pointee(IsCfgEdge(
                                    NodeIndexIs(1), NodeIndexIs(3), 10,
                                    CFGEdgeKind::kBranchOrFallthough))))));
  EXPECT_THAT(chains.at({10, {1, 0}})->inter_chain_in_edges(),
              UnorderedElementsAre(ChainIdIs(InterCfgId{10, {0, 0}})));
  EXPECT_THAT(
      chains.at({10, {2, 0}})->inter_chain_out_edges(),
      UnorderedElementsAre(Pair(
          chains.at({10, {4, 0}}).get(),
          ElementsAre(Pointee(IsCfgEdge(NodeIndexIs(2), NodeIndexIs(4), 250,
                                        CFGEdgeKind::kBranchOrFallthough))))));
  EXPECT_THAT(chains.at({10, {2, 0}})->inter_chain_in_edges(),
              UnorderedElementsAre(ChainIdIs(InterCfgId{10, {0, 0}}),
                                   ChainIdIs(InterCfgId{10, {1, 0}})));
  EXPECT_THAT(
      chains.at({10, {3, 0}})->inter_chain_out_edges(),
      UnorderedElementsAre(Pair(
          chains.at({10, {4, 0}}).get(),
          ElementsAre(Pointee(IsCfgEdge(NodeIndexIs(3), NodeIndexIs(4), 10,
                                        CFGEdgeKind::kBranchOrFallthough))))));
  EXPECT_THAT(chains.at({10, {3, 0}})->inter_chain_in_edges(),
              UnorderedElementsAre(ChainIdIs(InterCfgId{10, {1, 0}})));
  EXPECT_THAT(chains.at({10, {4, 0}})->inter_chain_out_edges(), IsEmpty());
  EXPECT_THAT(chains.at({10, {4, 0}})->inter_chain_in_edges(),
              UnorderedElementsAre(ChainIdIs(InterCfgId{10, {2, 0}}),
                                   ChainIdIs(InterCfgId{10, {3, 0}})));

  chain_builder.MergeChains(*chains.at({10, {1, 0}}),
                            *chains.at(InterCfgId{10, {3, 0}}));

  EXPECT_THAT(GetOrderedNodeIds(*chains.at({10, {1, 0}})),
              ElementsAre(InterCfgId{10, {1, 0}}, InterCfgId{10, {3, 0}}));

  EXPECT_THAT(
      chains.at({10, {0, 0}})->inter_chain_out_edges(),
      UnorderedElementsAre(Pair(chains.at({10, {1, 0}}).get(),
                                ElementsAre(Pointee(IsCfgEdge(
                                    NodeIndexIs(0), NodeIndexIs(1), 110,
                                    CFGEdgeKind::kBranchOrFallthough)))),
                           Pair(chains.at({10, {2, 0}}).get(),
                                ElementsAre(Pointee(IsCfgEdge(
                                    NodeIndexIs(0), NodeIndexIs(2), 150,
                                    CFGEdgeKind::kBranchOrFallthough))))));
  EXPECT_THAT(chains.at({10, {0, 0}})->inter_chain_in_edges(), IsEmpty());
  EXPECT_THAT(
      chains.at({10, {1, 0}})->inter_chain_out_edges(),
      UnorderedElementsAre(Pair(chains.at({10, {2, 0}}).get(),
                                ElementsAre(Pointee(IsCfgEdge(
                                    NodeIndexIs(1), NodeIndexIs(2), 100,
                                    CFGEdgeKind::kBranchOrFallthough)))),
                           Pair(chains.at({10, {4, 0}}).get(),
                                ElementsAre(Pointee(IsCfgEdge(
                                    NodeIndexIs(3), NodeIndexIs(4), 10,
                                    CFGEdgeKind::kBranchOrFallthough))))));
  EXPECT_THAT(chains.at({10, {1, 0}})->node_bundles(),
              ElementsAre(Pointee(HasIntraChainEdges(ElementsAre(Pointee(
                              IsCfgEdge(NodeIndexIs(1), NodeIndexIs(3), 10,
                                        CFGEdgeKind::kBranchOrFallthough))))),
                          Pointee(HasIntraChainEdges(IsEmpty()))));
  EXPECT_THAT(chains.at({10, {1, 0}})->inter_chain_in_edges(),
              UnorderedElementsAre(ChainIdIs(InterCfgId{10, {0, 0}})));
  EXPECT_THAT(
      chains.at({10, {2, 0}})->inter_chain_out_edges(),
      UnorderedElementsAre(Pair(
          chains.at({10, {4, 0}}).get(),
          ElementsAre(Pointee(IsCfgEdge(NodeIndexIs(2), NodeIndexIs(4), 250,
                                        CFGEdgeKind::kBranchOrFallthough))))));
  EXPECT_THAT(chains.at({10, {2, 0}})->inter_chain_in_edges(),
              UnorderedElementsAre(ChainIdIs(InterCfgId{10, {0, 0}}),
                                   ChainIdIs(InterCfgId{10, {1, 0}})));
  EXPECT_THAT(chains.at({10, {4, 0}})->inter_chain_out_edges(), IsEmpty());
  EXPECT_THAT(chains.at({10, {4, 0}})->inter_chain_in_edges(),
              UnorderedElementsAre(ChainIdIs(InterCfgId{10, {1, 0}}),
                                   ChainIdIs(InterCfgId{10, {2, 0}})));
}

// Check that MergeChain(NodeChainAssembly) properly updates the chain
// edges by calling MergeChainEdges.
TYPED_TEST(NodeChainBuilderTest, MergeChainsWithAssemblyUpdatesChainEdges) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ProtoProgramCfg> proto_program_cfg,
                       BuildFromCfgProtoPath(GetTestInputPath(
                           "_main/propeller/testdata/"
                           "simple_conditionals_join.protobuf")));
  ASSERT_THAT(proto_program_cfg->program_cfg().cfgs_by_index(),
              UnorderedElementsAre(Key(10)));

  PropellerStats::CodeLayoutStats stats;
  NodeChainBuilder chain_builder = this->InitializeNodeChainBuilderForCfgs(
      proto_program_cfg->program_cfg(), /*function_indices=*/{10},
      PropellerCodeLayoutParameters(), stats);
  chain_builder.InitNodeChains();
  chain_builder.InitChainEdges();
  const absl::flat_hash_map<InterCfgId, std::unique_ptr<NodeChain>> &chains =
      chain_builder.chains();

  ASSERT_THAT(chains, UnorderedElementsAre(Key(InterCfgId{10, {0, 0}}),
                                           Key(InterCfgId{10, {1, 0}}),
                                           Key(InterCfgId{10, {2, 0}}),
                                           Key(InterCfgId{10, {3, 0}}),
                                           Key(InterCfgId{10, {4, 0}})));
  chain_builder.MergeChains(*chains.at({10, {1, 0}}), *chains.at({10, {3, 0}}));
  absl::StatusOr<NodeChainAssembly> assembly =
      NodeChainAssembly::BuildNodeChainAssembly(
          chain_builder.node_to_bundle_mapper(),
          chain_builder.code_layout_scorer(), *chains.at({10, {1, 0}}),
          *chains.at({10, {2, 0}}), {.merge_order = ChainMergeOrder::kSU});
  ASSERT_OK(assembly);
  chain_builder.MergeChains(*assembly);
  EXPECT_THAT(GetOrderedNodeIds(*chains.at({10, {1, 0}})),
              ElementsAre(InterCfgId{10, {1, 0}}, InterCfgId{10, {3, 0}},
                          InterCfgId{10, {2, 0}}));

  EXPECT_THAT(
      chains.at({10, {0, 0}})->inter_chain_out_edges(),
      UnorderedElementsAre(Pair(
          chains.at({10, {1, 0}}).get(),
          ElementsAre(Pointee(IsCfgEdge(NodeIndexIs(0), NodeIndexIs(1), 110,
                                        CFGEdgeKind::kBranchOrFallthough)),
                      Pointee(IsCfgEdge(NodeIndexIs(0), NodeIndexIs(2), 150,
                                        CFGEdgeKind::kBranchOrFallthough))))));
  EXPECT_THAT(chains.at({10, {0, 0}})->inter_chain_in_edges(), IsEmpty());
  EXPECT_THAT(
      chains.at({10, {1, 0}})->inter_chain_out_edges(),
      UnorderedElementsAre(Pair(
          chains.at({10, {4, 0}}).get(),
          ElementsAre(Pointee(IsCfgEdge(NodeIndexIs(3), NodeIndexIs(4), 10,
                                        CFGEdgeKind::kBranchOrFallthough)),
                      Pointee(IsCfgEdge(NodeIndexIs(2), NodeIndexIs(4), 250,
                                        CFGEdgeKind::kBranchOrFallthough))))));
  EXPECT_THAT(
      chains.at({10, {1, 0}})->node_bundles(),
      ElementsAre(Pointee(HasIntraChainEdges(ElementsAre(
                      Pointee(IsCfgEdge(NodeIndexIs(1), NodeIndexIs(3), 10,
                                        CFGEdgeKind::kBranchOrFallthough)),
                      Pointee(IsCfgEdge(NodeIndexIs(1), NodeIndexIs(2), 100,
                                        CFGEdgeKind::kBranchOrFallthough))))),
                  Pointee(HasIntraChainEdges(IsEmpty())),
                  Pointee(HasIntraChainEdges(IsEmpty()))));

  EXPECT_THAT(chains.at({10, {1, 0}})->inter_chain_in_edges(),
              UnorderedElementsAre(ChainIdIs(InterCfgId{10, {0, 0}})));

  EXPECT_THAT(chains.at({10, {4, 0}})->inter_chain_out_edges(), IsEmpty());
  EXPECT_THAT(chains.at({10, {4, 0}})->inter_chain_in_edges(),
              UnorderedElementsAre(ChainIdIs(InterCfgId{10, {1, 0}})));
}

// Test GetForcedPaths and its two separate steps (GetForcedEdges and
// BreakCycles) in a CFG with a loop.
TEST(CodeLayoutTest, GetForcedPathsWithLoop) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ProtoProgramCfg> proto_program_cfg,
                       BuildFromCfgProtoPath(
                           GetTestInputPath("_main/propeller/testdata/"
                                            "loop_no_entry_no_exit.protobuf")));
  ASSERT_THAT(proto_program_cfg->program_cfg().cfgs_by_index(),
              UnorderedElementsAre(Key(0)));
  const ControlFlowGraph &foo_cfg =
      *proto_program_cfg->program_cfg().GetCfgByIndex(0);

  EXPECT_THAT(GetForcedPaths(foo_cfg),
              ElementsAre(ElementsAre(Pointee(NodeIndexIs(1)),
                                      Pointee(NodeIndexIs(2)))));

  absl::btree_map<const CFGNode *, const CFGNode *, CFGNodePtrComparator>
      forced_edges = GetForcedEdges(foo_cfg);
  EXPECT_THAT(forced_edges,
              UnorderedElementsAre(
                  Pair(Pointee(NodeIndexIs(1)), Pointee(NodeIndexIs(2))),
                  Pair(Pointee(NodeIndexIs(2)), Pointee(NodeIndexIs(1)))));

  BreakCycles(forced_edges);
  EXPECT_THAT(forced_edges,
              UnorderedElementsAre(
                  Pair(Pointee(NodeIndexIs(1)), Pointee(NodeIndexIs(2)))));
}

// Test GetForcedPaths and its two separate steps (GetForcedEdges and
// BreakCycles) in a CFG without a loop.
TEST(CodeLayoutTest, GetForcedPathsNoLoop) {
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ProtoProgramCfg> proto_program_cfg,
      BuildFromCfgProtoPath(GetTestInputPath("_main/propeller/testdata/"
                                             "three_branches.protobuf")));
  ASSERT_THAT(proto_program_cfg->program_cfg().cfgs_by_index(),
              UnorderedElementsAre(Key(0)));
  const ControlFlowGraph &foo_cfg =
      *proto_program_cfg->program_cfg().GetCfgByIndex(0);

  EXPECT_THAT(GetForcedPaths(foo_cfg),
              ElementsAre(ElementsAre(NodeIndexIs(0), NodeIndexIs(1)),
                          ElementsAre(NodeIndexIs(2), NodeIndexIs(3))));

  absl::btree_map<const CFGNode *, const CFGNode *, CFGNodePtrComparator>
      forced_edges = GetForcedEdges(foo_cfg);
  EXPECT_THAT(forced_edges,
              UnorderedElementsAre(
                  Pair(Pointee(NodeIndexIs(0)), Pointee(NodeIndexIs(1))),
                  Pair(Pointee(NodeIndexIs(2)), Pointee(NodeIndexIs(3)))));

  BreakCycles(forced_edges);
  EXPECT_THAT(forced_edges,
              UnorderedElementsAre(
                  Pair(Pointee(NodeIndexIs(0)), Pointee(NodeIndexIs(1))),
                  Pair(Pointee(NodeIndexIs(2)), Pointee(NodeIndexIs(3)))));
}

TYPED_TEST(NodeChainBuilderTest, InitNodeChainsCreatesBundlesForLoop) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ProtoProgramCfg> proto_program_cfg,
                       BuildFromCfgProtoPath(
                           GetTestInputPath("_main/propeller/testdata/"
                                            "loop_no_entry_no_exit.protobuf")));
  ASSERT_THAT(proto_program_cfg->program_cfg().cfgs_by_index(),
              UnorderedElementsAre(Key(0)));

  PropellerStats::CodeLayoutStats stats;
  NodeChainBuilder chain_builder = this->InitializeNodeChainBuilderForCfgs(
      proto_program_cfg->program_cfg(), /*function_indices=*/{0},
      PropellerCodeLayoutParameters(), stats);
  chain_builder.InitNodeChains();
  // Verify the initial chains.
  EXPECT_THAT(chain_builder.chains(),
              UnorderedElementsAre(
                  Pair(InterCfgId{0, {0, 0}},
                       Pointee(ResultOf(&GetOrderedNodeIds<NodeChain>,
                                        ElementsAre(InterCfgId{0, {0, 0}})))),
                  Pair(InterCfgId{0, {1, 0}},
                       Pointee(ResultOf(&GetOrderedNodeIds<NodeChain>,
                                        ElementsAre(InterCfgId{0, {1, 0}},
                                                    InterCfgId{0, {2, 0}}))))));
}

// This tests every step in NodeChainBuilder::BuildChains on a single CFG.
TYPED_TEST(NodeChainBuilderTest, BuildChainsSingleCfgInternal) {
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ProtoProgramCfg> proto_program_cfg,
      BuildFromCfgProtoPath(GetTestInputPath("_main/propeller/testdata/"
                                             "three_branches.protobuf")));
  ASSERT_THAT(proto_program_cfg->program_cfg().cfgs_by_index(),
              UnorderedElementsAre(Key(0)));
  const ControlFlowGraph &foo_cfg =
      *proto_program_cfg->program_cfg().GetCfgByIndex(0);
  ASSERT_THAT(foo_cfg.nodes(), SizeIs(6));
  PropellerStats::CodeLayoutStats stats;
  NodeChainBuilder chain_builder = this->InitializeNodeChainBuilderForCfgs(
      proto_program_cfg->program_cfg(), /*function_indices=*/{0},
      PropellerCodeLayoutParameters(), stats);
  chain_builder.InitNodeChains();

  const auto &chains = chain_builder.chains();
  // Verify the initial chains to make sure bundles are created.
  EXPECT_THAT(chains,
              UnorderedElementsAre(
                  Pair(InterCfgId{0, {0, 0}},
                       Pointee(ResultOf(&GetOrderedNodeIds<NodeChain>,
                                        ElementsAre(InterCfgId{0, {0, 0}},
                                                    InterCfgId{0, {1, 0}})))),
                  Pair(InterCfgId{0, {2, 0}},
                       Pointee(ResultOf(&GetOrderedNodeIds<NodeChain>,
                                        ElementsAre(InterCfgId{0, {2, 0}},
                                                    InterCfgId{0, {3, 0}})))),
                  Pair(InterCfgId{0, {4, 0}},
                       Pointee(ResultOf(&GetOrderedNodeIds<NodeChain>,
                                        ElementsAre(InterCfgId{0, {4, 0}})))),
                  Pair(InterCfgId{0, {5, 0}},
                       Pointee(ResultOf(&GetOrderedNodeIds<NodeChain>,
                                        ElementsAre(InterCfgId{0, {5, 0}}))))));

  chain_builder.InitChainEdges();

  // Verify the number of in edges and out edges of every chain.
  struct {
    InterCfgId chain_id;
    int inter_chain_out_edges_count;
    int inter_chain_in_edges_count;
  } expected_edge_counts[] = {{{0, {0, 0}}, 2, 0},
                              {{0, {2, 0}}, 0, 0},
                              {{0, {4, 0}}, 0, 1},
                              {{0, {5, 0}}, 0, 1}};
  for (const auto &chain_edge_count : expected_edge_counts) {
    EXPECT_EQ(
        chains.at(chain_edge_count.chain_id)->inter_chain_out_edges().size(),
        chain_edge_count.inter_chain_out_edges_count);
    EXPECT_EQ(
        chains.at(chain_edge_count.chain_id)->inter_chain_in_edges().size(),
        chain_edge_count.inter_chain_in_edges_count);
  }
  chain_builder.InitChainAssemblies();

  int merge_chain_count = 0;
  while (!chain_builder.node_chain_assemblies().empty()) {
    chain_builder.MergeChains(
        chain_builder.node_chain_assemblies().GetBestAssembly());
    ++merge_chain_count;
  }

  EXPECT_EQ(merge_chain_count, 2);
  // Verify that the chain assemblies is empty now.
  EXPECT_TRUE(chain_builder.node_chain_assemblies().empty());

  // Verify the constructed chains.
  EXPECT_THAT(
      chains,
      UnorderedElementsAre(
          Pair(InterCfgId{0, {0, 0}},
               Pointee(ResultOf(
                   &GetOrderedNodeIds<NodeChain>,
                   ElementsAre(InterCfgId{0, {0, 0}}, InterCfgId{0, {1, 0}},
                               InterCfgId{0, {4, 0}}, InterCfgId{0, {5, 0}})))),
          Pair(InterCfgId{0, {2, 0}},
               Pointee(ResultOf(&GetOrderedNodeIds<NodeChain>,
                                ElementsAre(InterCfgId{0, {2, 0}},
                                            InterCfgId{0, {3, 0}}))))));

  chain_builder.CoalesceChains();

  // Verify that the two chains are coalesced together.
  EXPECT_THAT(
      chains,
      UnorderedElementsAre(Pair(
          InterCfgId{0, {0, 0}},
          Pointee(ResultOf(
              &GetOrderedNodeIds<NodeChain>,
              ElementsAre(InterCfgId{0, {0, 0}}, InterCfgId{0, {1, 0}},
                          InterCfgId{0, {4, 0}}, InterCfgId{0, {5, 0}},
                          InterCfgId{0, {2, 0}}, InterCfgId{0, {3, 0}}))))));
}

// This tests NodeChainBuilder::BuildChains on multiple CFGs (with
// inter-procedural layout).
TYPED_TEST(NodeChainBuilderTest, BuildChainsMultipleCfgsInterFunction) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ProtoProgramCfg> proto_program_cfg,
                       BuildFromCfgProtoPath(
                           GetTestInputPath("_main/propeller/testdata/"
                                            "simple_multi_function.protobuf")));
  ASSERT_THAT(proto_program_cfg->program_cfg().cfgs_by_index(),
              UnorderedElementsAre(Key(0), Key(1), Key(4), Key(100)));

  PropellerStats::CodeLayoutStats stats;
  PropellerCodeLayoutParameters code_layout_params;
  code_layout_params.set_inter_function_reordering(true);
  NodeChainBuilder chain_builder = this->InitializeNodeChainBuilderForCfgs(
      proto_program_cfg->program_cfg(),
      /*function_indices=*/{0, 1, 4, 100}, code_layout_params, stats);

  // Verify the constructed chains.
  EXPECT_THAT(
      chain_builder.BuildChains(),
      UnorderedElementsAre(
          Pointee(ResultOf(
              &GetOrderedNodeIds<NodeChain>,
              ElementsAre(InterCfgId{1, {0, 0}}, InterCfgId{1, {1, 0}},
                          InterCfgId{1, {3, 0}}, InterCfgId{0, {0, 0}},
                          InterCfgId{0, {2, 0}}, InterCfgId{0, {1, 0}}))),
          Pointee(ResultOf(
              &GetOrderedNodeIds<NodeChain>,
              ElementsAre(InterCfgId{1, {2, 0}}, InterCfgId{1, {4, 0}}))),
          Pointee(ResultOf(&GetOrderedNodeIds<NodeChain>,
                           ElementsAre(InterCfgId{100, {0, 0}})))));
}

struct ChainBuilderSplitThresholdTestCase {
  std::string test_name;
  std::vector<int> function_indices;
  int chain_split_threshold;
  // Matcher for `node_bundles()` of the single `NodeChain` built.
  testing::Matcher<std::vector<std::unique_ptr<CFGNodeBundle>>> bundles_matcher;
};

using ChainBuilderSplitThresholdTest =
    ::testing::TestWithParam<ChainBuilderSplitThresholdTestCase>;

TEST_P(ChainBuilderSplitThresholdTest, BuildChains) {
  const ChainBuilderSplitThresholdTestCase &test_case = GetParam();

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ProtoProgramCfg> proto_program_cfg,
                       BuildFromCfgProtoPath(
                           GetTestInputPath("_main/propeller/testdata/"
                                            "call_from_simple_loop.protobuf")));
  ASSERT_THAT(proto_program_cfg->program_cfg().cfgs_by_index(),
              UnorderedElementsAre(Key(1), Key(2), Key(3)));
  PropellerStats::CodeLayoutStats stats;
  PropellerCodeLayoutParameters code_layout_params;
  code_layout_params.set_inter_function_reordering(true);
  code_layout_params.set_chain_split_threshold(test_case.chain_split_threshold);
  code_layout_params.set_chain_split(true);
  EXPECT_THAT(CreateNodeChainBuilderForCfgs(proto_program_cfg->program_cfg(),
                                            test_case.function_indices,
                                            code_layout_params, stats)
                  .BuildChains(),
              UnorderedElementsAre(Pointee(Property(
                  &NodeChain::node_bundles, test_case.bundles_matcher))));
}

INSTANTIATE_TEST_SUITE_P(
    ChainBuilderSplitThresholdTests, ChainBuilderSplitThresholdTest,
    testing::ValuesIn<ChainBuilderSplitThresholdTestCase>(
        {{.test_name = "Rebundles1",
          .function_indices = {1, 2},
          .chain_split_threshold = 2,
          .bundles_matcher = ElementsAre(
              Pointee(ResultOf(&GetOrderedNodeIds<CFGNodeBundle>,
                               ElementsAre(InterCfgId{1, {0, 0}},
                                           InterCfgId{1, {1, 0}},
                                           InterCfgId{1, {2, 0}}))),
              Pointee(ResultOf(&GetOrderedNodeIds<CFGNodeBundle>,
                               ElementsAre(InterCfgId{2, {0, 0}}))))},
         {.test_name = "Rebundles2",
          .function_indices = {1, 2, 3},
          .chain_split_threshold = 3,
          .bundles_matcher = ElementsAre(
              Pointee(ResultOf(&GetOrderedNodeIds<CFGNodeBundle>,
                               ElementsAre(InterCfgId{3, {0, 0}}))),
              Pointee(ResultOf(&GetOrderedNodeIds<CFGNodeBundle>,
                               ElementsAre(InterCfgId{1, {0, 0}},
                                           InterCfgId{1, {1, 0}},
                                           InterCfgId{1, {2, 0}}))),
              Pointee(ResultOf(&GetOrderedNodeIds<CFGNodeBundle>,
                               ElementsAre(InterCfgId{2, {0, 0}}))))},
         {.test_name = "DoesNotRebundle1",
          .function_indices = {1, 2},
          .chain_split_threshold = 3,
          .bundles_matcher = ElementsAre(
              Pointee(ResultOf(&GetOrderedNodeIds<CFGNodeBundle>,
                               ElementsAre(InterCfgId{1, {0, 0}}))),
              Pointee(ResultOf(&GetOrderedNodeIds<CFGNodeBundle>,
                               ElementsAre(InterCfgId{1, {1, 0}},
                                           InterCfgId{1, {2, 0}}))),
              Pointee(ResultOf(&GetOrderedNodeIds<CFGNodeBundle>,
                               ElementsAre(InterCfgId{2, {0, 0}}))))},
         {.test_name = "DoesNotRebundle2",
          .function_indices = {1, 2},
          .chain_split_threshold = 4,
          .bundles_matcher = ElementsAre(
              Pointee(ResultOf(&GetOrderedNodeIds<CFGNodeBundle>,
                               ElementsAre(InterCfgId{1, {0, 0}}))),
              Pointee(ResultOf(&GetOrderedNodeIds<CFGNodeBundle>,
                               ElementsAre(InterCfgId{1, {1, 0}},
                                           InterCfgId{1, {2, 0}}))),
              Pointee(ResultOf(&GetOrderedNodeIds<CFGNodeBundle>,
                               ElementsAre(InterCfgId{2, {0, 0}}))))},
         {.test_name = "DoesNotRebundle3",
          .function_indices = {1, 2, 3},
          .chain_split_threshold = 4,
          .bundles_matcher = ElementsAre(
              Pointee(ResultOf(&GetOrderedNodeIds<CFGNodeBundle>,
                               ElementsAre(InterCfgId{3, {0, 0}}))),
              Pointee(ResultOf(&GetOrderedNodeIds<CFGNodeBundle>,
                               ElementsAre(InterCfgId{1, {0, 0}}))),
              Pointee(ResultOf(&GetOrderedNodeIds<CFGNodeBundle>,
                               ElementsAre(InterCfgId{1, {1, 0}},
                                           InterCfgId{1, {2, 0}}))),
              Pointee(ResultOf(&GetOrderedNodeIds<CFGNodeBundle>,
                               ElementsAre(InterCfgId{2, {0, 0}}))))}}),
    [](const testing::TestParamInfo<ChainBuilderSplitThresholdTest::ParamType>
           &info) { return info.param.test_name; });

// This tests NodeChainBuilder::BuildChains on a single CFG (with
// non-inter-procedural layout).
TYPED_TEST(NodeChainBuilderTest, BuildChainsSingleCfg) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ProtoProgramCfg> proto_program_cfg,
                       BuildFromCfgProtoPath(
                           GetTestInputPath("_main/propeller/testdata/"
                                            "simple_multi_function.protobuf")));
  ASSERT_THAT(proto_program_cfg->program_cfg().cfgs_by_index(),
              UnorderedElementsAre(Key(0), Key(1), Key(4), Key(100)));

  PropellerStats::CodeLayoutStats stats;
  NodeChainBuilder chain_builder = this->InitializeNodeChainBuilderForCfgs(
      proto_program_cfg->program_cfg(), /*function_indices=*/{1},
      PropellerCodeLayoutParameters(), stats);

  // Verify the constructed chains.
  EXPECT_THAT(chain_builder.BuildChains(),
              UnorderedElementsAre(Pointee(ResultOf(
                  &GetOrderedNodeIds<NodeChain>,
                  ElementsAre(InterCfgId{1, {0, 0}}, InterCfgId{1, {1, 0}},
                              InterCfgId{1, {3, 0}}, InterCfgId{1, {2, 0}},
                              InterCfgId{1, {4, 0}})))));
}

TYPED_TEST(NodeChainBuilderTest, LargeBlocksPreventMerge) {
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ProtoProgramCfg> proto_program_cfg,
      BuildFromCfgProtoPath(GetTestInputPath("_main/propeller/testdata/"
                                             "two_large_blocks.protobuf")));
  ASSERT_THAT(proto_program_cfg->program_cfg().cfgs_by_index(),
              UnorderedElementsAre(Key(0), Key(1)));
  PropellerStats::CodeLayoutStats stats;
  PropellerCodeLayoutParameters code_layout_params;
  code_layout_params.set_inter_function_reordering(true);
  NodeChainBuilder chain_builder = this->InitializeNodeChainBuilderForCfgs(
      proto_program_cfg->program_cfg(), /*function_indices=*/{0, 1},
      // Use inter-function-reordering to disable coalescing.
      code_layout_params, stats);

  // Verify the constructed chains. 4 will not be merged with 1,2,3 because 1
  // and 3 are so large that the 2->4 edge provides no score gain.
  EXPECT_THAT(chain_builder.BuildChains(),
              UnorderedElementsAre(
                  Pointee(ResultOf(
                      &GetOrderedNodeIds<NodeChain>,
                      ElementsAre(InterCfgId{0, {0, 0}}, InterCfgId{0, {1, 0}},
                                  InterCfgId{0, {2, 0}}))),
                  Pointee(ResultOf(&GetOrderedNodeIds<NodeChain>,
                                   ElementsAre(InterCfgId{0, {3, 0}},
                                               InterCfgId{1, {0, 0}})))));
}

// Tests building and applying a SU NodeChainAssembly.
TEST(NodeChainAssemblyTest, ApplySUChainMergeOrder) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ProtoProgramCfg> proto_program_cfg,
                       BuildFromCfgProtoPath(GetTestInputPath(
                           "_main/propeller/testdata/"
                           "simple_conditionals_join.protobuf")));
  ASSERT_THAT(proto_program_cfg->program_cfg().cfgs_by_index(),
              UnorderedElementsAre(Key(10)));
  PropellerStats::CodeLayoutStats stats;
  NodeChainBuilder chain_builder = CreateNodeChainBuilderForCfgs(
      proto_program_cfg->program_cfg(), /*function_indices=*/{10},
      PropellerCodeLayoutParameters(), stats);
  chain_builder.InitNodeChains();
  chain_builder.InitChainEdges();
  const absl::flat_hash_map<InterCfgId, std::unique_ptr<NodeChain>> &chains =
      chain_builder.chains();
  ASSERT_THAT(chains, UnorderedElementsAre(Key(InterCfgId{10, {0, 0}}),
                                           Key(InterCfgId{10, {1, 0}}),
                                           Key(InterCfgId{10, {2, 0}}),
                                           Key(InterCfgId{10, {3, 0}}),
                                           Key(InterCfgId{10, {4, 0}})));

  ASSERT_OK_AND_ASSIGN(
      NodeChainAssembly assembly,
      NodeChainAssembly::BuildNodeChainAssembly(
          chain_builder.node_to_bundle_mapper(),
          chain_builder.code_layout_scorer(), *chains.at({10, {0, 0}}),
          *chains.at({10, {2, 0}}), {.merge_order = ChainMergeOrder::kSU}));
  EXPECT_THAT(assembly.score_gain(), DoubleNear(1500, kEpsilon));
  EXPECT_THAT(GetSliceIndices(chain_builder.node_to_bundle_mapper(), assembly,
                              *chain_builder.cfgs().front()),
              UnorderedElementsAre(Pair(InterCfgId{10, {0, 0}}, 0),
                                   Pair(InterCfgId{10, {2, 0}}, 1),
                                   Pair(InterCfgId{10, {1, 0}}, std::nullopt),
                                   Pair(InterCfgId{10, {3, 0}}, std::nullopt),
                                   Pair(InterCfgId{10, {4, 0}}, std::nullopt)));
  EXPECT_THAT(GetOrderedNodeIds(assembly),
              ElementsAre(InterCfgId{10, {0, 0}}, InterCfgId{10, {2, 0}}));
  chain_builder.MergeChains(assembly);
  EXPECT_THAT(GetOrderedNodeIds(*chains.at({10, {0, 0}})),
              ElementsAre(InterCfgId{10, {0, 0}}, InterCfgId{10, {2, 0}}));
  EXPECT_THAT(chains, UnorderedElementsAre(Key(InterCfgId{10, {0, 0}}),
                                           Key(InterCfgId{10, {1, 0}}),
                                           Key(InterCfgId{10, {3, 0}}),
                                           Key(InterCfgId{10, {4, 0}})));
}

// Tests building and applying a S1US2 NodeChainAssembly.
TEST(NodeChainAssemblyTest, ApplyS1US2ChainMergeOrder) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ProtoProgramCfg> proto_program_cfg,
                       BuildFromCfgProtoPath(GetTestInputPath(
                           "_main/propeller/testdata/"
                           "simple_conditionals_join.protobuf")));
  ASSERT_THAT(proto_program_cfg->program_cfg().cfgs_by_index(),
              UnorderedElementsAre(Key(10)));
  PropellerStats::CodeLayoutStats stats;
  NodeChainBuilder chain_builder = CreateNodeChainBuilderForCfgs(
      proto_program_cfg->program_cfg(), /*function_indices=*/{10},
      PropellerCodeLayoutParameters(), stats);
  chain_builder.InitNodeChains();
  chain_builder.InitChainEdges();
  const absl::flat_hash_map<InterCfgId, std::unique_ptr<NodeChain>> &chains =
      chain_builder.chains();
  ASSERT_THAT(chains, UnorderedElementsAre(Key(InterCfgId{10, {0, 0}}),
                                           Key(InterCfgId{10, {1, 0}}),
                                           Key(InterCfgId{10, {2, 0}}),
                                           Key(InterCfgId{10, {3, 0}}),
                                           Key(InterCfgId{10, {4, 0}})));

  chain_builder.MergeChains(*chains.at({10, {0, 0}}), *chains.at({10, {2, 0}}));
  EXPECT_THAT(chains, UnorderedElementsAre(Key(InterCfgId{10, {0, 0}}),
                                           Key(InterCfgId{10, {1, 0}}),
                                           Key(InterCfgId{10, {3, 0}}),
                                           Key(InterCfgId{10, {4, 0}})));
  EXPECT_THAT(GetOrderedNodeIds(*chains.at({10, {0, 0}})),
              ElementsAre(InterCfgId{10, {0, 0}}, InterCfgId{10, {2, 0}}));

  ASSERT_OK_AND_ASSIGN(
      NodeChainAssembly assembly,
      NodeChainAssembly::BuildNodeChainAssembly(
          chain_builder.node_to_bundle_mapper(),
          chain_builder.code_layout_scorer(), *chains.at({10, {0, 0}}),
          *chains.at({10, {1, 0}}),
          {.merge_order = ChainMergeOrder::kS1US2, .slice_pos = 1}));

  EXPECT_THAT(GetSliceIndices(chain_builder.node_to_bundle_mapper(), assembly,
                              *chain_builder.cfgs().front()),
              UnorderedElementsAre(Pair(InterCfgId{10, {0, 0}}, 0),
                                   Pair(InterCfgId{10, {1, 0}}, 1),
                                   Pair(InterCfgId{10, {2, 0}}, 2),
                                   Pair(InterCfgId{10, {3, 0}}, std::nullopt),
                                   Pair(InterCfgId{10, {4, 0}}, std::nullopt)));
  EXPECT_THAT(assembly.score_gain(), DoubleNear(749.414, kEpsilon));
  EXPECT_THAT(GetOrderedNodeIds(assembly),
              ElementsAre(InterCfgId{10, {0, 0}}, InterCfgId{10, {1, 0}},
                          InterCfgId{10, {2, 0}}));
  chain_builder.MergeChains(assembly);
  EXPECT_THAT(GetOrderedNodeIds(*chains.at({10, {0, 0}})),
              ElementsAre(InterCfgId{10, {0, 0}}, InterCfgId{10, {1, 0}},
                          InterCfgId{10, {2, 0}}));
  EXPECT_THAT(chains, UnorderedElementsAre(Key(InterCfgId{10, {0, 0}}),
                                           Key(InterCfgId{10, {3, 0}}),
                                           Key(InterCfgId{10, {4, 0}})));
}

// Test for building and applying a US2S1 NodeChainAssembly.
TEST(NodeChainAssemblyTest, ApplyUS2S1ChainMergeOrder) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ProtoProgramCfg> proto_program_cfg,
                       BuildFromCfgProtoPath(GetTestInputPath(
                           "_main/propeller/testdata/"
                           "simple_conditionals_join.protobuf")));
  ASSERT_THAT(proto_program_cfg->program_cfg().cfgs_by_index(),
              UnorderedElementsAre(Key(10)));
  PropellerStats::CodeLayoutStats stats;
  NodeChainBuilder chain_builder = CreateNodeChainBuilderForCfgs(
      proto_program_cfg->program_cfg(), /*function_indices=*/{10},
      PropellerCodeLayoutParameters(), stats);
  chain_builder.InitNodeChains();
  chain_builder.InitChainEdges();
  const absl::flat_hash_map<InterCfgId, std::unique_ptr<NodeChain>> &chains =
      chain_builder.chains();
  ASSERT_THAT(chains, UnorderedElementsAre(Key(InterCfgId{10, {0, 0}}),
                                           Key(InterCfgId{10, {1, 0}}),
                                           Key(InterCfgId{10, {2, 0}}),
                                           Key(InterCfgId{10, {3, 0}}),
                                           Key(InterCfgId{10, {4, 0}})));

  chain_builder.MergeChains(*chains.at({10, {2, 0}}), *chains.at({10, {1, 0}}));
  EXPECT_THAT(chains, UnorderedElementsAre(Key(InterCfgId{10, {0, 0}}),
                                           Key(InterCfgId{10, {2, 0}}),
                                           Key(InterCfgId{10, {3, 0}}),
                                           Key(InterCfgId{10, {4, 0}})));
  EXPECT_THAT(GetOrderedNodeIds(*chains.at({10, {2, 0}})),
              ElementsAre(InterCfgId{10, {2, 0}}, InterCfgId{10, {1, 0}}));

  ASSERT_OK_AND_ASSIGN(
      NodeChainAssembly assembly,
      NodeChainAssembly::BuildNodeChainAssembly(
          chain_builder.node_to_bundle_mapper(),
          chain_builder.code_layout_scorer(), *chains.at({10, {2, 0}}),
          *chains.at({10, {0, 0}}),
          {.merge_order = ChainMergeOrder::kUS2S1, .slice_pos = 1}));
  EXPECT_THAT(GetSliceIndices(chain_builder.node_to_bundle_mapper(), assembly,
                              *chain_builder.cfgs().front()),
              UnorderedElementsAre(Pair(InterCfgId{10, {0, 0}}, 0),
                                   Pair(InterCfgId{10, {1, 0}}, 1),
                                   Pair(InterCfgId{10, {2, 0}}, 2),
                                   Pair(InterCfgId{10, {3, 0}}, std::nullopt),
                                   Pair(InterCfgId{10, {4, 0}}, std::nullopt)));
  EXPECT_THAT(assembly.score_gain(), DoubleNear(2150.00230, kEpsilon));
  EXPECT_THAT(GetOrderedNodeIds(assembly),
              ElementsAre(InterCfgId{10, {0, 0}}, InterCfgId{10, {1, 0}},
                          InterCfgId{10, {2, 0}}));
  chain_builder.MergeChains(assembly);
  EXPECT_THAT(chains, UnorderedElementsAre(Key(InterCfgId{10, {2, 0}}),
                                           Key(InterCfgId{10, {3, 0}}),
                                           Key(InterCfgId{10, {4, 0}})));
  EXPECT_THAT(GetOrderedNodeIds(*chains.at({10, {2, 0}})),
              ElementsAre(InterCfgId{10, {0, 0}}, InterCfgId{10, {1, 0}},
                          InterCfgId{10, {2, 0}}));
}

// Tests building and applying a S2S1U NodeChainAssembly.
TEST(NodeChainAssemblyTest, ApplyS2S1UChainMergeOrder) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ProtoProgramCfg> proto_program_cfg,
                       BuildFromCfgProtoPath(GetTestInputPath(
                           "_main/propeller/testdata/"
                           "simple_conditionals_join.protobuf")));
  ASSERT_THAT(proto_program_cfg->program_cfg().cfgs_by_index(),
              UnorderedElementsAre(Key(10)));
  PropellerStats::CodeLayoutStats stats;
  NodeChainBuilder chain_builder = CreateNodeChainBuilderForCfgs(
      proto_program_cfg->program_cfg(), /*function_indices=*/{10},
      PropellerCodeLayoutParameters(), stats);
  chain_builder.InitNodeChains();
  chain_builder.InitChainEdges();
  const absl::flat_hash_map<InterCfgId, std::unique_ptr<NodeChain>> &chains =
      chain_builder.chains();
  ASSERT_THAT(chains, UnorderedElementsAre(Key(InterCfgId{10, {0, 0}}),
                                           Key(InterCfgId{10, {1, 0}}),
                                           Key(InterCfgId{10, {2, 0}}),
                                           Key(InterCfgId{10, {3, 0}}),
                                           Key(InterCfgId{10, {4, 0}})));

  chain_builder.MergeChains(*chains.at({10, {2, 0}}), *chains.at({10, {1, 0}}));
  EXPECT_THAT(chains, UnorderedElementsAre(Key(InterCfgId{10, {0, 0}}),
                                           Key(InterCfgId{10, {2, 0}}),
                                           Key(InterCfgId{10, {3, 0}}),
                                           Key(InterCfgId{10, {4, 0}})));
  EXPECT_THAT(GetOrderedNodeIds(*chains.at({10, {2, 0}})),
              ElementsAre(InterCfgId{10, {2, 0}}, InterCfgId{10, {1, 0}}));

  ASSERT_OK_AND_ASSIGN(
      NodeChainAssembly assembly,
      NodeChainAssembly::BuildNodeChainAssembly(
          chain_builder.node_to_bundle_mapper(),
          chain_builder.code_layout_scorer(), *chains.at({10, {2, 0}}),
          *chains.at({10, {3, 0}}),
          {.merge_order = ChainMergeOrder::kS2S1U, .slice_pos = 1}));

  EXPECT_THAT(GetSliceIndices(chain_builder.node_to_bundle_mapper(), assembly,
                              *chain_builder.cfgs().front()),
              UnorderedElementsAre(Pair(InterCfgId{10, {0, 0}}, std::nullopt),
                                   Pair(InterCfgId{10, {1, 0}}, 0),
                                   Pair(InterCfgId{10, {2, 0}}, 1),
                                   Pair(InterCfgId{10, {3, 0}}, 2),
                                   Pair(InterCfgId{10, {4, 0}}, std::nullopt)));
  EXPECT_THAT(assembly.score_gain(), DoubleNear(1000.58824, kEpsilon));
  EXPECT_THAT(GetOrderedNodeIds(assembly),
              ElementsAre(InterCfgId{10, {1, 0}}, InterCfgId{10, {2, 0}},
                          InterCfgId{10, {3, 0}}));
  chain_builder.MergeChains(assembly);
  EXPECT_THAT(chains, UnorderedElementsAre(Key(InterCfgId{10, {0, 0}}),
                                           Key(InterCfgId{10, {2, 0}}),
                                           Key(InterCfgId{10, {4, 0}})));
  EXPECT_THAT(GetOrderedNodeIds(*chains.at({10, {2, 0}})),
              ElementsAre(InterCfgId{10, {1, 0}}, InterCfgId{10, {2, 0}},
                          InterCfgId{10, {3, 0}}));
}

// Tests building and applying a S2US1 NodeChainAssembly.
TEST(NodeChainAssemblyTest, ApplyS2US1ChainMergeOrder) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ProtoProgramCfg> proto_program_cfg,
                       BuildFromCfgProtoPath(GetTestInputPath(
                           "_main/propeller/testdata/"
                           "simple_conditionals_join.protobuf")));
  ASSERT_THAT(proto_program_cfg->program_cfg().cfgs_by_index(),
              UnorderedElementsAre(Key(10)));
  PropellerStats::CodeLayoutStats stats;
  NodeChainBuilder chain_builder = CreateNodeChainBuilderForCfgs(
      proto_program_cfg->program_cfg(), /*function_indices=*/{10},
      PropellerCodeLayoutParameters(), stats);
  chain_builder.InitNodeChains();
  chain_builder.InitChainEdges();
  const absl::flat_hash_map<InterCfgId, std::unique_ptr<NodeChain>> &chains =
      chain_builder.chains();
  ASSERT_THAT(chains, UnorderedElementsAre(Key(InterCfgId{10, {0, 0}}),
                                           Key(InterCfgId{10, {1, 0}}),
                                           Key(InterCfgId{10, {2, 0}}),
                                           Key(InterCfgId{10, {3, 0}}),
                                           Key(InterCfgId{10, {4, 0}})));

  chain_builder.MergeChains(*chains.at({10, {2, 0}}), *chains.at({10, {1, 0}}));
  EXPECT_THAT(chains, UnorderedElementsAre(Key(InterCfgId{10, {0, 0}}),
                                           Key(InterCfgId{10, {2, 0}}),
                                           Key(InterCfgId{10, {3, 0}}),
                                           Key(InterCfgId{10, {4, 0}})));
  EXPECT_THAT(GetOrderedNodeIds(*chains.at({10, {2, 0}})),
              ElementsAre(InterCfgId{10, {2, 0}}, InterCfgId{10, {1, 0}}));

  ASSERT_OK_AND_ASSIGN(
      NodeChainAssembly assembly,
      NodeChainAssembly::BuildNodeChainAssembly(
          chain_builder.node_to_bundle_mapper(),
          chain_builder.code_layout_scorer(), *chains.at({10, {2, 0}}),
          *chains.at({10, {3, 0}}),
          {.merge_order = ChainMergeOrder::kS2US1, .slice_pos = 1}));
  EXPECT_THAT(GetSliceIndices(chain_builder.node_to_bundle_mapper(), assembly,
                              *chain_builder.cfgs().front()),
              UnorderedElementsAre(Pair(InterCfgId{10, {0, 0}}, std::nullopt),
                                   Pair(InterCfgId{10, {1, 0}}, 0),
                                   Pair(InterCfgId{10, {2, 0}}, 2),
                                   Pair(InterCfgId{10, {3, 0}}, 1),
                                   Pair(InterCfgId{10, {4, 0}}, std::nullopt)));
  EXPECT_THAT(assembly.score_gain(), DoubleNear(100.39292, kEpsilon));
  EXPECT_THAT(GetOrderedNodeIds(assembly),
              ElementsAre(InterCfgId{10, {1, 0}}, InterCfgId{10, {3, 0}},
                          InterCfgId{10, {2, 0}}));
  chain_builder.MergeChains(assembly);
  EXPECT_THAT(chains, UnorderedElementsAre(Key(InterCfgId{10, {0, 0}}),
                                           Key(InterCfgId{10, {2, 0}}),
                                           Key(InterCfgId{10, {4, 0}})));
  EXPECT_THAT(GetOrderedNodeIds(*chains.at({10, {2, 0}})),
              ElementsAre(InterCfgId{10, {1, 0}}, InterCfgId{10, {3, 0}},
                          InterCfgId{10, {2, 0}}));
}

struct NodeChainAssemblyBuildStatusTestCase {
  std::string test_name;
  // Pairs of chain ids which must be merged in order by
  // `NodeChainBuilder::MergeChains(left_chain, right_chain)` before the assert
  // step. The first element is the `left_chain` id and the second element is
  // the `right_chain` id.
  std::vector<std::pair<InterCfgId, InterCfgId>> setup_merge_chain_ids;
  InterCfgId split_chain_id;
  InterCfgId unsplit_chain_id;
  NodeChainAssembly::NodeChainAssemblyBuildingOptions options;
  Matcher<absl::StatusOr<NodeChainAssembly>> status_matcher;
};

using NodeChainAssemblyBuildStatusTest =
    ::testing::TestWithParam<NodeChainAssemblyBuildStatusTestCase>;

TEST_P(NodeChainAssemblyBuildStatusTest, TestBuildNodeChainAssemblyStatus) {
  const NodeChainAssemblyBuildStatusTestCase &test_case = GetParam();

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ProtoProgramCfg> proto_program_cfg,
                       BuildFromCfgProtoPath(GetTestInputPath(
                           "_main/propeller/testdata/"
                           "simple_conditionals_join.protobuf")));
  ASSERT_THAT(proto_program_cfg->program_cfg().cfgs_by_index(),
              UnorderedElementsAre(Key(10)));
  PropellerStats::CodeLayoutStats stats;
  NodeChainBuilder chain_builder = CreateNodeChainBuilderForCfgs(
      proto_program_cfg->program_cfg(), /*function_indices=*/{10},
      PropellerCodeLayoutParameters(), stats);
  chain_builder.InitNodeChains();
  chain_builder.InitChainEdges();
  const absl::flat_hash_map<InterCfgId, std::unique_ptr<NodeChain>> &chains =
      chain_builder.chains();
  ASSERT_THAT(chains, UnorderedElementsAre(Key(InterCfgId{10, {0, 0}}),
                                           Key(InterCfgId{10, {1, 0}}),
                                           Key(InterCfgId{10, {2, 0}}),
                                           Key(InterCfgId{10, {3, 0}}),
                                           Key(InterCfgId{10, {4, 0}})));
  for (const auto &[left_chain_id, right_chain_id] :
       test_case.setup_merge_chain_ids)
    chain_builder.MergeChains(*chains.at(left_chain_id),
                              *chains.at(right_chain_id));

  EXPECT_THAT(NodeChainAssembly::BuildNodeChainAssembly(
                  chain_builder.node_to_bundle_mapper(),
                  chain_builder.code_layout_scorer(),
                  *chains.at(test_case.split_chain_id),
                  *chains.at(test_case.unsplit_chain_id), test_case.options),
              test_case.status_matcher);
}

INSTANTIATE_TEST_SUITE_P(
    NodeChainAssemblyBuildStatusTests, NodeChainAssemblyBuildStatusTest,
    testing::ValuesIn<NodeChainAssemblyBuildStatusTestCase>(
        {{.test_name = "EntryInMiddleS2S1UMultiNode",
          .setup_merge_chain_ids = {{{10, {0, 0}}, {10, {1, 0}}}},
          .split_chain_id = {10, {0, 0}},
          .unsplit_chain_id = {10, {2, 0}},
          .options = {.merge_order = ChainMergeOrder::kS2S1U, .slice_pos = 1},
          .status_matcher =
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       "Assembly places the entry block in the middle.")},
         {.test_name = "EntryInMiddleSUMultiNode",
          .split_chain_id = {10, {1, 0}},
          .unsplit_chain_id = {10, {0, 0}},
          .options = {.merge_order = ChainMergeOrder::kSU},
          .status_matcher =
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       "Assembly places the entry block in the middle.")},
         {.test_name = "ZeroScoreGainError",
          .split_chain_id = {10, {0, 0}},
          .unsplit_chain_id = {10, {3, 0}},
          .options = {.merge_order = ChainMergeOrder::kSU},
          .status_matcher = StatusIs(absl::StatusCode::kFailedPrecondition,
                                     "Assembly has zero score gain.")},
         {.test_name = "ZeroScoreGainOK",
          .split_chain_id = {10, {0, 0}},
          .unsplit_chain_id = {10, {3, 0}},
          .options = {.merge_order = ChainMergeOrder::kSU,
                      .error_on_zero_score_gain = false},
          .status_matcher = IsOk()},
         {.test_name = "EntryInMiddleSUSingleNode",
          .split_chain_id = {10, {1, 0}},
          .unsplit_chain_id = {10, {0, 0}},
          .options = {.merge_order = ChainMergeOrder::kSU},
          .status_matcher =
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       "Assembly places the entry block in the middle.")},
         {.test_name = "NegativeScoreGainS1US2Error1",
          .setup_merge_chain_ids = {{{10, {0, 0}}, {10, {1, 0}}}},
          .split_chain_id = {10, {0, 0}},
          .unsplit_chain_id = {10, {3, 0}},
          .options = {.merge_order = ChainMergeOrder::kS1US2, .slice_pos = 1},
          .status_matcher =
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       "Assembly has negative score gain: -980.303079")},
         {.test_name = "NegativeScoreGainS1US2Error2",
          .setup_merge_chain_ids = {{{10, {0, 0}}, {10, {1, 0}}}},
          .split_chain_id = {10, {0, 0}},
          .unsplit_chain_id = {10, {3, 0}},
          .options = {.merge_order = ChainMergeOrder::kS1US2,
                      .slice_pos = 1,
                      .error_on_zero_score_gain = false},
          .status_matcher =
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       "Assembly has negative score gain: -980.303079")}}),
    [](const testing::TestParamInfo<NodeChainAssemblyBuildStatusTest::ParamType>
           &info) { return info.param.test_name; });

struct NodeChainAssemblyBuildDeathTestCase {
  std::string test_name;
  std::vector<std::pair<InterCfgId, InterCfgId>> setup_merge_chain_ids;
  InterCfgId split_chain_id;
  InterCfgId unsplit_chain_id;
  NodeChainAssembly::NodeChainAssemblyBuildingOptions options;
  std::string expected_error;
};

using NodeChainAssemblyBuildDeathTest =
    ::testing::TestWithParam<NodeChainAssemblyBuildDeathTestCase>;

TEST_P(NodeChainAssemblyBuildDeathTest, TestNodeChainAssemblyBuildDeath) {
  const NodeChainAssemblyBuildDeathTestCase &test_case = GetParam();

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ProtoProgramCfg> proto_program_cfg,
                       BuildFromCfgProtoPath(GetTestInputPath(
                           "_main/propeller/testdata/"
                           "simple_conditionals_join.protobuf")));
  ASSERT_THAT(proto_program_cfg->program_cfg().cfgs_by_index(),
              UnorderedElementsAre(Key(10)));
  PropellerStats::CodeLayoutStats stats;
  NodeChainBuilder chain_builder = CreateNodeChainBuilderForCfgs(
      proto_program_cfg->program_cfg(), /*function_indices=*/{10},
      PropellerCodeLayoutParameters(), stats);
  chain_builder.InitNodeChains();
  chain_builder.InitChainEdges();
  const absl::flat_hash_map<InterCfgId, std::unique_ptr<NodeChain>> &chains =
      chain_builder.chains();
  ASSERT_THAT(chains, UnorderedElementsAre(Key(InterCfgId{10, {0, 0}}),
                                           Key(InterCfgId{10, {1, 0}}),
                                           Key(InterCfgId{10, {2, 0}}),
                                           Key(InterCfgId{10, {3, 0}}),
                                           Key(InterCfgId{10, {4, 0}})));
  for (const auto &[left_chain_id, right_chain_id] :
       test_case.setup_merge_chain_ids)
    chain_builder.MergeChains(*chains.at(left_chain_id),
                              *chains.at(right_chain_id));

  EXPECT_DEATH(NodeChainAssembly::BuildNodeChainAssembly(
                   chain_builder.node_to_bundle_mapper(),
                   chain_builder.code_layout_scorer(),
                   *chains.at(test_case.split_chain_id),
                   *chains.at(test_case.unsplit_chain_id), test_case.options)
                   .IgnoreError(),
               test_case.expected_error);
}

INSTANTIATE_TEST_SUITE_P(
    NodeChainAssemblyBuildDeathTests, NodeChainAssemblyBuildDeathTest,
    testing::ValuesIn<NodeChainAssemblyBuildDeathTestCase>(
        {{.test_name = "SelfMerge",
          .setup_merge_chain_ids = {},
          .split_chain_id = {10, {0, 0}},
          .unsplit_chain_id = {10, {0, 0}},
          .options = {.merge_order = ChainMergeOrder::kSU},
          .expected_error =
              "Cannot construct an assembly between a chain and itself."},
         {.test_name = "SlicePosForSU",
          .setup_merge_chain_ids = {},
          .split_chain_id = {10, {0, 0}},
          .unsplit_chain_id = {10, {1, 0}},
          .options = {.merge_order = ChainMergeOrder::kSU, .slice_pos = 0},
          .expected_error =
              "slice_pos must not be provided for kSU merge order."},
         {.test_name = "OutOfBoundsSlicePosS2S1U",
          .setup_merge_chain_ids = {},
          .split_chain_id = {10, {0, 0}},
          .unsplit_chain_id = {10, {1, 0}},
          .options = {.merge_order = ChainMergeOrder::kS2S1U, .slice_pos = 0},
          .expected_error = "Out of bounds slice position."},
         {.test_name = "OutOfBoundsSlicePosS1US2",
          .setup_merge_chain_ids = {},
          .split_chain_id = {10, {1, 0}},
          .unsplit_chain_id = {10, {0, 0}},
          .options = {.merge_order = ChainMergeOrder::kS1US2, .slice_pos = 1},
          .expected_error = "Out of bounds slice position."},
         {.test_name = "NoSlicePosForUS2S1",
          .setup_merge_chain_ids = {},
          .split_chain_id = {10, {1, 0}},
          .unsplit_chain_id = {10, {0, 0}},
          .options = {.merge_order = ChainMergeOrder::kUS2S1},
          .expected_error =
              "slice_pos is required for every merge order other than kSU."},
         {.test_name = "OutOfBoundsSlicePosS2S1USetupMerge1",
          .setup_merge_chain_ids = {{{10, {0, 0}}, {10, {1, 0}}}},
          .split_chain_id = {10, {0, 0}},
          .unsplit_chain_id = {10, {2, 0}},
          .options = {.merge_order = ChainMergeOrder::kS2S1U, .slice_pos = 0},
          .expected_error = "Out of bounds slice position."},
         {.test_name = "OutOfBoundsSlicePosS2S1USetupMerge2",
          .setup_merge_chain_ids = {{{10, {0, 0}}, {10, {1, 0}}}},
          .split_chain_id = {10, {0, 0}},
          .unsplit_chain_id = {10, {2, 0}},
          .options = {.merge_order = ChainMergeOrder::kS2S1U, .slice_pos = 2},
          .expected_error = "Out of bounds slice position."},
         {.test_name = "OutOfBoundsSlicePosUS2S1SetupMerge1",
          .setup_merge_chain_ids = {{{10, {0, 0}}, {10, {1, 0}}}},
          .split_chain_id = {10, {0, 0}},
          .unsplit_chain_id = {10, {2, 0}},
          .options = {.merge_order = ChainMergeOrder::kUS2S1, .slice_pos = 0},
          .expected_error = "Out of bounds slice position."},
         {.test_name = "OutOfBoundsSlicePosUS2S1SetupMerge2",
          .setup_merge_chain_ids = {{{10, {0, 0}}, {10, {1, 0}}}},
          .split_chain_id = {10, {0, 0}},
          .unsplit_chain_id = {10, {2, 0}},
          .options = {.merge_order = ChainMergeOrder::kUS2S1, .slice_pos = 2},
          .expected_error = "Out of bounds slice position."},
         {.test_name = "SelfMergeSetupMerge",
          .setup_merge_chain_ids = {{{10, {0, 0}}, {10, {1, 0}}}},
          .split_chain_id = {10, {0, 0}},
          .unsplit_chain_id = {10, {0, 0}},
          .options = {.merge_order = ChainMergeOrder::kS1US2, .slice_pos = 0},
          .expected_error =
              "Cannot construct an assembly between a chain and itself."}}),
    [](const testing::TestParamInfo<NodeChainAssemblyBuildDeathTest::ParamType>
           &info) { return info.param.test_name; });

// Test for ChainClusterBuilder::BuildClusters on three functions.
TEST(CodeLayoutTest, BuildClusters) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ProtoProgramCfg> proto_program_cfg,
                       BuildFromCfgProtoPath(
                           GetTestInputPath("_main/propeller/testdata/"
                                            "simple_multi_function.protobuf")));

  std::vector<std::unique_ptr<const NodeChain>> built_chains;
  PropellerStats::CodeLayoutStats stats;
  for (const ControlFlowGraph *cfg :
       proto_program_cfg->program_cfg().GetCfgs()) {
    absl::c_move(NodeChainBuilder::CreateNodeChainBuilder(
                     PropellerCodeLayoutScorer(PropellerCodeLayoutParameters()),
                     {cfg}, /*initial_chains=*/{}, stats)
                     .BuildChains(),
                 std::back_inserter(built_chains));
  }

  // Verify that the input to the code under test (BuildClusters) is as
  // expected.
  CHECK_EQ(built_chains.size(), 3);
  // Chain for function foo.
  CHECK(GetOrderedNodeIds(*built_chains[0]) ==
        std::vector<InterCfgId>({{0, {0, 0}}, {0, {2, 0}}, {0, {1, 0}}}));
  // Chain for function bar.
  CHECK(GetOrderedNodeIds(*built_chains[1]) ==
        std::vector<InterCfgId>(
            {{1, {0, 0}}, {1, {1, 0}}, {1, {3, 0}}, {1, {2, 0}}, {1, {4, 0}}}));
  // Chain for function qux.
  CHECK(GetOrderedNodeIds(*built_chains[2]) ==
        std::vector<InterCfgId>({{100, {0, 0}}}));

  // Verify the final clusters.
  PropellerCodeLayoutParameters params;
  params.set_call_chain_clustering(true);
  EXPECT_THAT(
      ChainClusterBuilder(params, std::move(built_chains)).BuildClusters(),
      // Chains of foo and bar are merged into one cluster.
      ElementsAre(
          Pointee(ResultOf(
              &GetOrderedNodeIds<ChainCluster>,
              ElementsAre(InterCfgId{1, {0, 0}}, InterCfgId{1, {1, 0}},
                          InterCfgId{1, {3, 0}}, InterCfgId{1, {2, 0}},
                          InterCfgId{1, {4, 0}}, InterCfgId{0, {0, 0}},
                          InterCfgId{0, {2, 0}}, InterCfgId{0, {1, 0}}))),
          // Cluster containing the single block of qux, which won't
          // be merged with any other chain.
          Pointee(ResultOf(&GetOrderedNodeIds<ChainCluster>,
                           ElementsAre(InterCfgId{100, {0, 0}})))));
}

TEST(CodeLayoutTest, FindOptimalFallthroughNoSplitChains) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ProtoProgramCfg> proto_program_cfg,
                       BuildFromCfgProtoPath(GetTestInputPath(
                           "_main/propeller/testdata/"
                           "two_conditionals_in_loop.protobuf")));

  ASSERT_THAT(proto_program_cfg->program_cfg().cfgs_by_index(), SizeIs(1));
  PropellerCodeLayoutParameters params;
  params.set_chain_split(false);
  std::vector<FunctionChainInfo> all_func_chain_info =
      CodeLayout(params, proto_program_cfg->program_cfg().GetCfgs()).OrderAll();
  ASSERT_THAT(all_func_chain_info, SizeIs(1));
  auto &func_chain_info = all_func_chain_info[0];
  EXPECT_THAT(all_func_chain_info,
              ElementsAre(FunctionChainInfoIs(
                  22,
                  ElementsAre(HasFullBbIds(ElementsAre(
                      BbIdIs(0), BbIdIs(1), BbIdIs(2), BbIdIs(4), BbIdIs(3)))),
                  _, _, _)));
  // Verify that the new layout improves the score.
  EXPECT_GT(func_chain_info.optimized_score.intra_score,
            func_chain_info.original_score.intra_score);
}

TEST(CodeLayoutTest, FindOptimalFallthroughSplitChains) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ProtoProgramCfg> proto_program_cfg,
                       BuildFromCfgProtoPath(GetTestInputPath(
                           "_main/propeller/testdata/"
                           "two_conditionals_in_loop.protobuf")));

  ASSERT_THAT(proto_program_cfg->program_cfg().cfgs_by_index(), SizeIs(1));
  PropellerCodeLayoutParameters params;
  params.set_chain_split(true);
  std::vector<FunctionChainInfo> all_func_chain_info =
      CodeLayout(params, proto_program_cfg->program_cfg().GetCfgs()).OrderAll();
  ASSERT_THAT(all_func_chain_info, SizeIs(1));
  auto &func_chain_info = all_func_chain_info[0];
  EXPECT_THAT(func_chain_info,
              FunctionChainInfoIs(
                  22,
                  ElementsAre(HasFullBbIds(ElementsAre(
                      BbIdIs(0), BbIdIs(1), BbIdIs(3), BbIdIs(2), BbIdIs(4)))),
                  _, _, _));
  // Verify that the new layout improves the score.
  EXPECT_GT(func_chain_info.optimized_score.intra_score,
            func_chain_info.original_score.intra_score);
}

TEST(CodeLayoutTest, FindOptimalLoopLayout) {
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ProtoProgramCfg> proto_program_cfg,
      BuildFromCfgProtoPath(GetTestInputPath("_main/propeller/testdata/"
                                             "simple_loop.protobuf")));

  ASSERT_THAT(proto_program_cfg->program_cfg().cfgs_by_index(), SizeIs(1));
  std::vector<FunctionChainInfo> all_func_chain_info =
      CodeLayout(PropellerCodeLayoutParameters(),
                 proto_program_cfg->program_cfg().GetCfgs())
          .OrderAll();
  ASSERT_THAT(all_func_chain_info, SizeIs(1));
  auto &func_chain_info = all_func_chain_info[0];
  EXPECT_THAT(
      func_chain_info,
      FunctionChainInfoIs(0,
                          ElementsAre(HasFullBbIds(ElementsAre(
                              BbIdIs(0), BbIdIs(1), BbIdIs(3), BbIdIs(4)))),
                          _, _, _));
  // Verify that the new layout improves the score.
  EXPECT_GT(func_chain_info.optimized_score.intra_score,
            func_chain_info.original_score.intra_score);
}

TEST(CodeLayoutTest, FindOptimalNestedLoopLayout) {
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ProtoProgramCfg> proto_program_cfg,
      BuildFromCfgProtoPath(GetTestInputPath("_main/propeller/testdata/"
                                             "nested_loop.protobuf")));
  ASSERT_THAT(proto_program_cfg->program_cfg().cfgs_by_index(), SizeIs(1));
  std::vector<FunctionChainInfo> all_func_chain_info =
      CodeLayout(PropellerCodeLayoutParameters(),
                 proto_program_cfg->program_cfg().GetCfgs())
          .OrderAll();
  ASSERT_THAT(all_func_chain_info, SizeIs(1));
  auto &func_chain_info = all_func_chain_info[0];
  EXPECT_THAT(func_chain_info,
              FunctionChainInfoIs(_,
                                  ElementsAre(HasFullBbIds(ElementsAre(
                                      BbIdIs(0), BbIdIs(3), BbIdIs(1),
                                      BbIdIs(4), BbIdIs(5), BbIdIs(2)))),
                                  _, _, _));
  // Verify that the new layout improves the score.
  EXPECT_GT(func_chain_info.optimized_score.intra_score,
            func_chain_info.original_score.intra_score);
}

TEST(CodeLayoutTest, FindOptimalMultiFunctionLayout) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ProtoProgramCfg> proto_program_cfg,
                       BuildFromCfgProtoPath(
                           GetTestInputPath("_main/propeller/testdata/"
                                            "simple_multi_function.protobuf")));

  PropellerCodeLayoutParameters params;
  params.set_call_chain_clustering(true);
  std::vector<FunctionChainInfo> all_func_chain_info =
      CodeLayout(params, proto_program_cfg->program_cfg().GetCfgs()).OrderAll();

  EXPECT_THAT(
      all_func_chain_info,
      ElementsAre(
          FunctionChainInfoIs(
              0,
              ElementsAre(BbChainIs(
                  1, ElementsAre(BbBundleIs(ElementsAre(BbIdIs(0))),
                                 BbBundleIs(ElementsAre(BbIdIs(2))),
                                 BbBundleIs(ElementsAre(BbIdIs(1)))))),
              CfgScoreIsNear(98.82353, 0, kEpsilon),
              CfgScoreIsNear(819.88281, 0, kEpsilon), 1),
          FunctionChainInfoIs(
              1,
              ElementsAre(BbChainIs(
                  0,
                  ElementsAre(
                      BbBundleIs(ElementsAre(BbIdIs(0), BbIdIs(1), BbIdIs(3))),
                      BbBundleIs(ElementsAre(BbIdIs(2), BbIdIs(4)))))),
              CfgScoreIsNear(199.62353, 99.55882, kEpsilon),
              CfgScoreIsNear(2020.00000, 97.36328, kEpsilon), 0),
          FunctionChainInfoIs(
              100,
              ElementsAre(BbChainIs(
                  2, ElementsAre(BbBundleIs(ElementsAre(BbIdIs(0)))))),
              CfgScoreIsNear(9.91176, 0, kEpsilon),
              CfgScoreIsNear(9.91176, 0, kEpsilon), 2)));
}

TEST(CodeLayoutTest, FindLayoutNoReorderHotBlocks) {
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ProtoProgramCfg> proto_program_cfg,
      BuildFromCfgProtoPath(GetTestInputPath("_main/propeller/testdata/"
                                             "multiple_cold_blocks.protobuf")));

  PropellerCodeLayoutParameters params;
  params.set_reorder_hot_blocks(false);
  EXPECT_THAT(
      CodeLayout(params, proto_program_cfg->program_cfg().GetCfgs()).OrderAll(),
      _);
}

TEST(CodeLayoutTest, FindLayoutNoFunctionSplit) {
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ProtoProgramCfg> proto_program_cfg,
      BuildFromCfgProtoPath(GetTestInputPath("_main/propeller/testdata/"
                                             "multiple_cold_blocks.protobuf")));

  PropellerCodeLayoutParameters params;
  params.set_split_functions(false);
  EXPECT_THAT(
      CodeLayout(params, proto_program_cfg->program_cfg().GetCfgs()).OrderAll(),
      ElementsAre(FunctionChainInfoIs(
          999,
          ElementsAre(BbChainIs(
              0, ElementsAre(BbBundleIs(ElementsAre(BbIdIs(0))),
                             BbBundleIs(ElementsAre(BbIdIs(3), BbIdIs(1))),
                             BbBundleIs(ElementsAre(BbIdIs(2), BbIdIs(4)))))),
          _, _, 0)));
}

TEST(CodeLayoutTest, FindLayoutNoReorderHotBlocksNoFunctionSplit) {
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ProtoProgramCfg> proto_program_cfg,
      BuildFromCfgProtoPath(GetTestInputPath("_main/propeller/testdata/"
                                             "multiple_cold_blocks.protobuf")));

  PropellerCodeLayoutParameters params;
  params.set_split_functions(false);
  params.set_reorder_hot_blocks(false);
  EXPECT_THAT(
      CodeLayout(params, proto_program_cfg->program_cfg().GetCfgs()).OrderAll(),
      ElementsAre(FunctionChainInfoIs(
          999,
          ElementsAre(BbChainIs(
              0, ElementsAre(
                     BbBundleIs(ElementsAre(BbIdIs(0), BbIdIs(1), BbIdIs(3))),
                     BbBundleIs(ElementsAre(BbIdIs(2), BbIdIs(4)))))),
          _, _, 0)));
}

TEST(CodeLayoutTest, FindOptimalMultiFunctionLayoutInterFunction) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ProtoProgramCfg> proto_program_cfg,
                       BuildFromCfgProtoPath(
                           GetTestInputPath("_main/propeller/testdata/"
                                            "simple_multi_function.protobuf")));

  PropellerCodeLayoutParameters params;
  params.set_call_chain_clustering(true);
  params.set_inter_function_reordering(true);
  std::vector<FunctionChainInfo> all_func_chain_info =
      CodeLayout(params, proto_program_cfg->program_cfg().GetCfgs()).OrderAll();

  EXPECT_THAT(
      all_func_chain_info,
      ElementsAre(
          FunctionChainInfoIs(
              0,
              ElementsAre(BbChainIs(1, ElementsAre(BbBundleIs(ElementsAre(
                                           BbIdIs(0), BbIdIs(2), BbIdIs(1)))))),
              CfgScoreIsNear(98.82353, 0, kEpsilon),
              CfgScoreIsNear(819.88281, 0, kEpsilon), 1),
          FunctionChainInfoIs(
              1,
              ElementsAre(BbChainIs(0, ElementsAre(BbBundleIs(ElementsAre(
                                           BbIdIs(0), BbIdIs(1), BbIdIs(3))))),
                          BbChainIs(3, ElementsAre(BbBundleIs(ElementsAre(
                                           BbIdIs(2), BbIdIs(4)))))),
              CfgScoreIsNear(199.62353, 99.55882, kEpsilon),
              CfgScoreIsNear(2020.00000, 99.12109, kEpsilon), 0),
          FunctionChainInfoIs(
              100,
              ElementsAre(BbChainIs(
                  2, ElementsAre(BbBundleIs(ElementsAre(BbIdIs(0)))))),
              CfgScoreIsNear(9.91176, 0, kEpsilon),
              CfgScoreIsNear(9.91176, 0, kEpsilon), 2)));
}

TEST(CodeLayoutTest, PlacesBlocksBeforeEntryInInterFunctionOrdering) {
  std::unique_ptr<ProgramCfg> program_cfg = BuildFromCfgArg(
      {.cfg_args = {{".foo_section",
                     0,
                     "foo",
                     {{0x1000, 0, 0x10},
                      {0x1010, 1, 0x7},
                      {0x102a, 2, 0x40},
                      {0x1030, 3, 0x8}},
                     {{0, 1, 20, CFGEdgeKind::kBranchOrFallthough},
                      {0, 3, 10, CFGEdgeKind::kBranchOrFallthough},
                      {1, 2, 30, CFGEdgeKind::kBranchOrFallthough},
                      {2, 1, 40, CFGEdgeKind::kBranchOrFallthough}}}}});
  PropellerCodeLayoutParameters params;
  params.set_inter_function_reordering(true);
  std::vector<FunctionChainInfo> all_func_chain_info =
      CodeLayout(params, program_cfg->GetCfgs()).OrderAll();

  EXPECT_THAT(
      all_func_chain_info,
      ElementsAre(FunctionChainInfoIs(
          0,
          ElementsAre(
              BbChainIs(1, ElementsAre(
                               BbBundleIs(ElementsAre(BbIdIs(0))),
                               BbBundleIs(ElementsAre(BbIdIs(1), BbIdIs(2))))),
              BbChainIs(0, ElementsAre(BbBundleIs(ElementsAre(BbIdIs(3)))))),
          _, _, _)));
}

TEST(CodeLayoutTest, PlacesEntryBlockFirstInIntraFunctionOrdering) {
  std::unique_ptr<ProgramCfg> program_cfg = BuildFromCfgArg(
      {.cfg_args = {{".foo_section",
                     0,
                     "foo",
                     {{0x1000, 0, 0x10},
                      {0x1010, 1, 0x7},
                      {0x102a, 2, 0x40},
                      {0x1030, 3, 0x8}},
                     {{0, 1, 20, CFGEdgeKind::kBranchOrFallthough},
                      {0, 3, 10, CFGEdgeKind::kBranchOrFallthough},
                      {1, 2, 30, CFGEdgeKind::kBranchOrFallthough},
                      {2, 1, 40, CFGEdgeKind::kBranchOrFallthough}}}}});
  PropellerCodeLayoutParameters params;
  params.set_inter_function_reordering(false);
  std::vector<FunctionChainInfo> all_func_chain_info =
      CodeLayout(params, program_cfg->GetCfgs()).OrderAll();

  EXPECT_THAT(
      all_func_chain_info,
      ElementsAre(FunctionChainInfoIs(
          0,
          ElementsAre(BbChainIs(
              0, ElementsAre(BbBundleIs(ElementsAre(BbIdIs(0))),
                             BbBundleIs(ElementsAre(BbIdIs(1), BbIdIs(2))),
                             BbBundleIs(ElementsAre(BbIdIs(3)))))),
          _, _, _)));
}

TEST(CodeLayoutTest, FindOptimalLayoutHotAndColdLandingPads) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ProtoProgramCfg> proto_program_cfg,
                       BuildFromCfgProtoPath(GetTestInputPath(
                           "_main/propeller/testdata/"
                           "hot_and_cold_landing_pads.protobuf")));

  EXPECT_THAT(
      CodeLayout(PropellerCodeLayoutParameters(),
                 proto_program_cfg->program_cfg().GetCfgs())
          .OrderAll(),
      Contains(FunctionChainInfoIs(
          10,
          // Check that the cold landing pad block (#3) is merged
          // into the single chain for function 'foo'.
          ElementsAre(BbChainIs(
              _, ElementsAre(BbBundleIs(ElementsAre(BbIdIs(0))),
                             BbBundleIs(ElementsAre(BbIdIs(1), BbIdIs(4))),
                             BbBundleIs(ElementsAre(BbIdIs(2))),
                             BbBundleIs(ElementsAre(BbIdIs(5))),
                             BbBundleIs(ElementsAre(BbIdIs(3)))))),
          _, _, _)));
}

TEST(CodeLayoutTest, FindOptimalLayoutAllColdLandingPads) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ProtoProgramCfg> proto_program_cfg,
                       BuildFromCfgProtoPath(
                           GetTestInputPath("_main/propeller/testdata/"
                                            "all_cold_landing_pads.protobuf")));

  EXPECT_THAT(CodeLayout(PropellerCodeLayoutParameters(),
                         proto_program_cfg->program_cfg().GetCfgs())
                  .OrderAll(),
              Contains(FunctionChainInfoIs(
                  100,
                  // Check that landing pad blocks (#2, and #3) are not merged
                  // into the chain.
                  // This means they will be in the cold section.
                  ElementsAre(BbChainIs(
                      _, ElementsAre(BbBundleIs(ElementsAre(
                             BbIdIs(0), BbIdIs(1), BbIdIs(4), BbIdIs(5)))))),
                  _, _, _)));
}

TEST(CodeLayoutTest, FindOptimalInterFunctionLayoutHotAndColdLandingPads) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<ProtoProgramCfg> proto_program_cfg,
                       BuildFromCfgProtoPath(GetTestInputPath(
                           "_main/propeller/testdata/"
                           "hot_and_cold_landing_pads.protobuf")));

  PropellerCodeLayoutParameters params;
  params.set_inter_function_reordering(true);
  EXPECT_THAT(
      CodeLayout(params, proto_program_cfg->program_cfg().GetCfgs(),
                 /*initial_chains=*/{})
          .OrderAll(),
      Contains(FunctionChainInfoIs(
          10,
          // Check that for inter-function reordering, both landing pad
          // blocks (#2, and #3) are merged into the chain.
          ElementsAre(BbChainIs(_, ElementsAre(BbBundleIs(ElementsAre(
                                       BbIdIs(0), BbIdIs(1), BbIdIs(4),
                                       BbIdIs(2), BbIdIs(5), BbIdIs(3)))))),
          _, _, _)));
}

TEST(CodeLayoutTest, KeepsInitialChainsWithoutChainSplit) {
  std::unique_ptr<ProgramCfg> program_cfg = BuildFromCfgArg(
      {.cfg_args = {{".foo_section",
                     0,
                     "foo",
                     {{0x1000, 0, 0x10},
                      {0x1010, 1, 0x7},
                      {0x102a, 2, 0x40},
                      {0x1030, 3, 0x8}},
                     {{0, 1, 20, CFGEdgeKind::kBranchOrFallthough},
                      {0, 3, 10, CFGEdgeKind::kBranchOrFallthough},
                      {1, 2, 30, CFGEdgeKind::kBranchOrFallthough},
                      {2, 1, 40, CFGEdgeKind::kBranchOrFallthough}}}}});
  PropellerCodeLayoutParameters params;
  params.set_chain_split(false);
  std::vector<FunctionChainInfo> chain_info =
      CodeLayout(
          params, program_cfg->GetCfgs(),
          /*initial_chains=*/{{0, ConstructBbChains({{{{0, 0}}, {{2, 0}}}})}})
          .OrderAll();
  EXPECT_THAT(chain_info,
              ElementsAre(FunctionChainInfoIs(
                  0,
                  ElementsAre(BbChainIs(
                      0, ElementsAre(BbBundleIs(ElementsAre(BbIdIs(0))),
                                     BbBundleIs(ElementsAre(BbIdIs(2))),
                                     BbBundleIs(ElementsAre(BbIdIs(1))),
                                     BbBundleIs(ElementsAre(BbIdIs(3)))))),
                  _, _, _)));
}

TEST(CodeLayoutTest, KeepsMultipleInitialChainsWithoutChainSplit) {
  std::unique_ptr<ProgramCfg> program_cfg = BuildFromCfgArg(
      {.cfg_args = {{".foo_section",
                     0,
                     "foo",
                     {{0x1000, 0, 0x10},
                      {0x1010, 1, 0x7},
                      {0x102a, 2, 0x40},
                      {0x1030, 3, 0x8}},
                     {{0, 1, 20, CFGEdgeKind::kBranchOrFallthough},
                      {0, 3, 10, CFGEdgeKind::kBranchOrFallthough},
                      {1, 2, 30, CFGEdgeKind::kBranchOrFallthough},
                      {2, 1, 40, CFGEdgeKind::kBranchOrFallthough}}}}});
  PropellerCodeLayoutParameters params;
  params.set_chain_split(false);
  std::vector<FunctionChainInfo> chain_info =
      CodeLayout(params, program_cfg->GetCfgs(),
                 /*initial_chains=*/
                 {{0, ConstructBbChains(
                          {{{{0, 0}}, {{2, 0}}}, {{{3, 0}}, {{1, 0}}}})}})
          .OrderAll();
  EXPECT_THAT(chain_info,
              ElementsAre(FunctionChainInfoIs(
                  0,
                  ElementsAre(BbChainIs(
                      0, ElementsAre(BbBundleIs(ElementsAre(BbIdIs(0))),
                                     BbBundleIs(ElementsAre(BbIdIs(2))),
                                     BbBundleIs(ElementsAre(BbIdIs(3))),
                                     BbBundleIs(ElementsAre(BbIdIs(1)))))),
                  _, _, _)));
}

TEST(CodeLayoutTest, BreaksInitialChainsWithChainSplit) {
  std::unique_ptr<ProgramCfg> program_cfg = BuildFromCfgArg(
      {.cfg_args = {{".foo_section",
                     0,
                     "foo",
                     {{0x1000, 0, 0x10},
                      {0x1010, 1, 0x7},
                      {0x102a, 2, 0x40},
                      {0x1030, 3, 0x8}},
                     {{0, 1, 20, CFGEdgeKind::kBranchOrFallthough},
                      {0, 3, 10, CFGEdgeKind::kBranchOrFallthough},
                      {1, 2, 30, CFGEdgeKind::kBranchOrFallthough},
                      {2, 1, 40, CFGEdgeKind::kBranchOrFallthough}}}}});
  PropellerCodeLayoutParameters params;
  params.set_chain_split(true);
  std::vector<FunctionChainInfo> chain_info =
      CodeLayout(
          params, program_cfg->GetCfgs(),
          /*initial_chains=*/{{0, ConstructBbChains({{{{0, 0}}, {{2, 0}}}})}})
          .OrderAll();
  EXPECT_THAT(chain_info,
              ElementsAre(FunctionChainInfoIs(
                  0,
                  ElementsAre(BbChainIs(
                      0, ElementsAre(BbBundleIs(ElementsAre(BbIdIs(0))),
                                     BbBundleIs(ElementsAre(BbIdIs(1))),
                                     BbBundleIs(ElementsAre(BbIdIs(2))),
                                     BbBundleIs(ElementsAre(BbIdIs(3)))))),
                  _, _, _)));
}

TEST(CodeLayoutTest, KeepsProfitableInitialChainsWithChainSplit) {
  std::unique_ptr<ProgramCfg> program_cfg = BuildFromCfgArg(
      {.cfg_args = {{".foo_section",
                     0,
                     "foo",
                     {{0x1000, 0, 0x10},
                      {0x1010, 1, 0x7},
                      {0x102a, 2, 0x40},
                      {0x1030, 3, 0x8}},
                     {{0, 1, 20, CFGEdgeKind::kBranchOrFallthough},
                      {0, 3, 10, CFGEdgeKind::kBranchOrFallthough},
                      {1, 2, 30, CFGEdgeKind::kBranchOrFallthough},
                      {2, 1, 40, CFGEdgeKind::kBranchOrFallthough}}}}});
  PropellerCodeLayoutParameters params;
  params.set_chain_split(true);
  std::vector<FunctionChainInfo> chain_info =
      CodeLayout(
          params, program_cfg->GetCfgs(),
          /*initial_chains=*/{{0, ConstructBbChains({{{{1, 0}}, {{2, 0}}}})}})
          .OrderAll();
  EXPECT_THAT(chain_info,
              ElementsAre(FunctionChainInfoIs(
                  0,
                  ElementsAre(BbChainIs(
                      0, ElementsAre(BbBundleIs(ElementsAre(BbIdIs(0))),
                                     BbBundleIs(ElementsAre(BbIdIs(1))),
                                     BbBundleIs(ElementsAre(BbIdIs(2))),
                                     BbBundleIs(ElementsAre(BbIdIs(3)))))),
                  _, _, _)));
}

TEST(CodeLayoutTest, BreaksInitialChainsWithChainSplitEdgeFromMiddle) {
  std::unique_ptr<ProgramCfg> program_cfg = BuildFromCfgArg(
      {.cfg_args = {{".foo_section",
                     0,
                     "foo",
                     {{0x1000, 0, 0x10},
                      {0x1010, 1, 0x7},
                      {0x102a, 2, 0x40},
                      {0x1030, 3, 0x8}},
                     {{0, 1, 20, CFGEdgeKind::kBranchOrFallthough},
                      {0, 3, 10, CFGEdgeKind::kBranchOrFallthough},
                      {1, 2, 30, CFGEdgeKind::kBranchOrFallthough},
                      {2, 1, 40, CFGEdgeKind::kBranchOrFallthough}}}}});
  PropellerCodeLayoutParameters params;
  params.set_chain_split(true);
  std::vector<FunctionChainInfo> chain_info =
      CodeLayout(params, program_cfg->GetCfgs(),
                 /*initial_chains=*/
                 {{0, ConstructBbChains({{{{0, 0}, {1, 0}, {3, 0}}}})}})
          .OrderAll();
  EXPECT_THAT(chain_info,
              ElementsAre(FunctionChainInfoIs(
                  0,
                  ElementsAre(BbChainIs(
                      0, ElementsAre(BbBundleIs(ElementsAre(
                                         BbIdIs(0), BbIdIs(1), BbIdIs(3))),
                                     BbBundleIs(ElementsAre(BbIdIs(2)))))),
                  _, _, _)));
}

TEST(CodeLayoutTest, BreaksInitialChainsWithChainSplitEdgeToMiddle) {
  std::unique_ptr<ProgramCfg> program_cfg = BuildFromCfgArg(
      {.cfg_args = {{".foo_section",
                     0,
                     "foo",
                     {{0x1000, 0, 0x10},
                      {0x1010, 1, 0x7},
                      {0x102a, 2, 0x40},
                      {0x1030, 3, 0x8}},
                     {{0, 1, 20, CFGEdgeKind::kBranchOrFallthough},
                      {0, 3, 10, CFGEdgeKind::kBranchOrFallthough},
                      {1, 2, 30, CFGEdgeKind::kBranchOrFallthough},
                      {2, 1, 40, CFGEdgeKind::kBranchOrFallthough}}}}});
  PropellerCodeLayoutParameters params;
  params.set_chain_split(true);
  std::vector<FunctionChainInfo> chain_info =
      CodeLayout(params, program_cfg->GetCfgs(),
                 /*initial_chains=*/
                 {{0, ConstructBbChains({{{{1, 0}, {3, 0}, {2, 0}}}})}})
          .OrderAll();
  EXPECT_THAT(chain_info,
              ElementsAre(FunctionChainInfoIs(
                  0,
                  ElementsAre(BbChainIs(
                      0, ElementsAre(BbBundleIs(ElementsAre(BbIdIs(0))),
                                     BbBundleIs(ElementsAre(
                                         BbIdIs(1), BbIdIs(3), BbIdIs(2)))))),
                  _, _, _)));
}

TEST(CodeLayoutTest, FailsWithDuplicateNodesInInitialChains) {
  std::unique_ptr<ProgramCfg> program_cfg = BuildFromCfgArg(
      {.cfg_args = {{".foo_section",
                     0,
                     "foo",
                     {{0x1000, 0, 0x10},
                      {0x1010, 1, 0x7},
                      {0x102a, 2, 0x40},
                      {0x1030, 3, 0x8}},
                     {{0, 1, 20, CFGEdgeKind::kBranchOrFallthough},
                      {0, 3, 10, CFGEdgeKind::kBranchOrFallthough},
                      {1, 2, 30, CFGEdgeKind::kBranchOrFallthough},
                      {2, 1, 40, CFGEdgeKind::kBranchOrFallthough}}}}});
  PropellerCodeLayoutParameters params;
  params.set_chain_split(false);
  EXPECT_DEATH(
      CodeLayout(
          params, program_cfg->GetCfgs(),
          /*initial_chains=*/
          {{0, ConstructBbChains({{{{1, 0}, {2, 0}}, {{2, 0}, {3, 0}}}})}})
          .OrderAll(),
      HasSubstr("Node [function index: 0, [BB index: 2, clone number: 0]] is "
                "already in a bundle"));
}

TEST(NodeChainBuilderTest, SortsIntraChainEdges) {
  std::unique_ptr<ProgramCfg> program_cfg = BuildFromCfgArg(
      {.cfg_args = {{".foo_section",
                     0,
                     "foo",
                     {{0x1000, 0, 0x10},
                      {0x1010, 1, 0x7},
                      {0x102a, 2, 0x40},
                      {0x1030, 3, 0x8}},
                     {{0, 1, 20, CFGEdgeKind::kBranchOrFallthough},
                      {0, 3, 10, CFGEdgeKind::kBranchOrFallthough},
                      {0, 2, 30, CFGEdgeKind::kBranchOrFallthough},
                      {2, 1, 40, CFGEdgeKind::kBranchOrFallthough}}}}});
  PropellerStats::CodeLayoutStats stats;
  NodeChainBuilder chain_builder = NodeChainBuilder::CreateNodeChainBuilder(
      PropellerCodeLayoutScorer(PropellerCodeLayoutParameters()),
      {program_cfg->GetCfgByIndex(0)},
      /*initial_chains=*/
      {{0, ConstructBbChains({{{{3, 0}}, {{1, 0}}, {{0, 0}}, {{2, 0}}}})}},
      stats);
  chain_builder.InitNodeChains();
  chain_builder.InitChainEdges();
  // Verify that the intra-chain edges are sorted in the order of their sink
  // nodes' position in chain.
  EXPECT_THAT(
      chain_builder.chains(),
      UnorderedElementsAre(Pair(
          _,
          Pointee(Property(
              &NodeChain::node_bundles,
              ElementsAre(
                  _, _,
                  Pointee(Property(
                      &CFGNodeBundle::intra_chain_out_edges,
                      ElementsAre(Pointee(IsCfgEdge(NodeIndexIs(0),
                                                    NodeIndexIs(3), _, _)),
                                  Pointee(IsCfgEdge(NodeIndexIs(0),
                                                    NodeIndexIs(1), _, _)),
                                  Pointee(IsCfgEdge(NodeIndexIs(0),
                                                    NodeIndexIs(2), _, _))))),
                  _))))));
}

}  // namespace
}  // namespace propeller
