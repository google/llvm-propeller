#ifndef PROPELLER_MOCK_PROGRAM_CFG_BUILDER_H_
#define PROPELLER_MOCK_PROGRAM_CFG_BUILDER_H_

#include <memory>
#include <string>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "llvm/Support/Allocator.h"
#include "propeller/cfg.h"
#include "propeller/cfg_testutil.h"
#include "propeller/program_cfg.h"

namespace propeller {

// Represents a whole program cfg constructed from a test protobuf.
class ProtoProgramCfg {
 public:
  ProtoProgramCfg(
      std::unique_ptr<llvm::BumpPtrAllocator> bump_ptr_allocator,
      absl::flat_hash_map<int, std::unique_ptr<ControlFlowGraph>> cfgs)
      : program_cfg_(std::move(cfgs)),
        bump_ptr_allocator_(std::move(bump_ptr_allocator)) {}

  const ProgramCfg &program_cfg() const { return program_cfg_; }

 private:
  const ProgramCfg program_cfg_;
  std::unique_ptr<llvm::BumpPtrAllocator> bump_ptr_allocator_;
};

// Constructs and returns a `ProtoProgramCfg` from a a protobuf file stored in
// `path_to_cfg_proto`.
absl::StatusOr<std::unique_ptr<ProtoProgramCfg>> BuildFromCfgProtoPath(
    const std::string &path_to_cfg_proto);

// Constructs and returns a `ProgramCfg` from a the given `multi_cfg_arg`.
std::unique_ptr<ProgramCfg> BuildFromCfgArg(MultiCfgArg multi_cfg_arg);
}  // namespace propeller

#endif  // PROPELLER_MOCK_PROGRAM_CFG_BUILDER_H_
