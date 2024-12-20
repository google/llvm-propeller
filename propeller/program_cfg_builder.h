#ifndef PROPELLER_PROGRAM_CFG_BUILDER_H_
#define PROPELLER_PROGRAM_CFG_BUILDER_H_

#include <memory>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "propeller/addr2cu.h"
#include "propeller/binary_address_mapper.h"
#include "propeller/branch_aggregation.h"
#include "propeller/cfg.h"
#include "propeller/cfg_edge.h"
#include "propeller/cfg_edge_kind.h"
#include "propeller/cfg_id.h"
#include "propeller/cfg_node.h"
#include "propeller/program_cfg.h"
#include "propeller/propeller_statistics.h"

namespace propeller {

class ProgramCfgBuilder {
 public:
  // Constructs a `ProgramCfgBuilder` initialized with CFGs in `program_cfg` (if
  // not nullptr) and which uses `binary_address_mapper` to map binary addresses
  // to basic blocks.
  // Does not take ownership of `binary_address_mapper`, which must refer to a
  // valid `BinaryAddressMapper` that outlives the constructed
  // `ProgramCfgBuilder`.
  ProgramCfgBuilder(const BinaryAddressMapper *binary_address_mapper,
                    PropellerStats &stats)
      : binary_address_mapper_(binary_address_mapper), stats_(&stats) {}

  ProgramCfgBuilder(const ProgramCfgBuilder &) = delete;
  ProgramCfgBuilder &operator=(const ProgramCfgBuilder &) = delete;
  ProgramCfgBuilder(ProgramCfgBuilder &&) = default;
  ProgramCfgBuilder &operator=(ProgramCfgBuilder &&) = default;

  // Creates profile CFGs using the branch profile in `branch_aggregation`.
  // `addr2cu`, if provided, will be used to retrieve module names for CFGs.
  // This function does not assume ownership of it.
  absl::StatusOr<std::unique_ptr<ProgramCfg>> Build(
      const BranchAggregation &branch_aggregation,
      Addr2Cu *addr2cu = nullptr) &&;

 private:
  // Creates and returns an edge from `from_bb` to `to_bb` (specified by their
  // BbHandle index) with the given `weight` and `edge_kind` and associates it
  // to the corresponding nodes specified by `tmp_node_map`. Finally inserts the
  // edge into `tmp_edge_map` with the key being the pair `{from_bb, to_bb}`.
  CFGEdge *InternalCreateEdge(
      int from_bb_index, int to_bb_index, int weight, CFGEdgeKind edge_kind,
      const absl::flat_hash_map<InterCfgId, CFGNode *> &tmp_node_map,
      absl::flat_hash_map<std::pair<InterCfgId, InterCfgId>, CFGEdge *>
          *tmp_edge_map);

  void CreateFallthroughs(
      const BranchAggregation &branch_aggregation,
      const absl::flat_hash_map<InterCfgId, CFGNode *> &tmp_node_map,
      absl::flat_hash_map<std::pair<int, int>, int>
          *tmp_bb_fallthrough_counters,
      absl::flat_hash_map<std::pair<InterCfgId, InterCfgId>, CFGEdge *>
          *tmp_edge_map);

  // Create control flow graph edges from branch_counters_. For each address
  // pair
  // <from_addr, to_addr> in "branch_counters_", we translate it to
  // <from_symbol, to_symbol> and by using tmp_node_map, we further translate
  // it to <from_node, to_node>, and finally create a CFGEdge for such CFGNode
  // pair.
  absl::Status CreateEdges(
      const BranchAggregation &branch_aggregation,
      const absl::flat_hash_map<InterCfgId, CFGNode *> &node_map);

  const BinaryAddressMapper *binary_address_mapper_;
  PropellerStats *stats_;
  // Maps from function index to its CFG.
  absl::flat_hash_map<int, std::unique_ptr<ControlFlowGraph>> cfgs_;
};
}  // namespace propeller

#endif  // PROPELLER_PROGRAM_CFG_BUILDER_H_
