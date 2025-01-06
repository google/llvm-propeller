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

#include "propeller/mock_program_cfg_builder.h"

#include <fcntl.h>  // for "O_RDONLY"

#include <cerrno>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "google/protobuf/io/zero_copy_stream_impl.h"  // for "proto2::io::FileInputStream"
#include "google/protobuf/text_format.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Object/ELFTypes.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/StringSaver.h"
#include "propeller/cfg.h"
#include "propeller/cfg.pb.h"
#include "propeller/cfg_edge.h"
#include "propeller/cfg_edge_kind.h"
#include "propeller/cfg_id.h"
#include "propeller/cfg_node.h"
#include "propeller/cfg_testutil.h"
#include "propeller/program_cfg.h"

namespace propeller {
namespace {
using ::llvm::object::BBAddrMap;

CFGEdgeKind ConvertFromPb(CFGEdgePb::Kind kindpb) {
  switch (kindpb) {
    case CFGEdgePb::BRANCH_OR_FALLTHROUGH:
      return CFGEdgeKind::kBranchOrFallthough;
    case CFGEdgePb::CALL:
      return CFGEdgeKind::kCall;
    case CFGEdgePb::RETURN:
      return CFGEdgeKind::kRet;
  }
}

InterCfgId ConvertFromPb(const CFGEdgePb::NodeId &idpb) {
  return InterCfgId{static_cast<int>(idpb.function_index()),
                    {static_cast<int>(idpb.bb_index()), 0}};
}

BBAddrMap::BBEntry::Metadata ConvertFromPb(
    const CFGNodePb::MetadataPb &metadatapb) {
  return {.HasReturn = metadatapb.has_return(),
          .HasTailCall = metadatapb.has_tail_call(),
          .IsEHPad = metadatapb.is_landing_pad(),
          .CanFallThrough = metadatapb.is_landing_pad()};
}

std::unique_ptr<CFGNode> CreateNodeFromNodePb(int function_index,
                                              const CFGNodePb &nodepb) {
  return std::make_unique<CFGNode>(
      /*addr=*/0,
      /*bb_index=*/nodepb.bb_id(),
      /*bb_id=*/nodepb.bb_id(), /*size=*/nodepb.size(),
      /*metadata=*/ConvertFromPb(nodepb.metadata()),
      /*function_index=*/function_index);
}

// Creates control flow graphs from protobuf.
// Calls `CalculateNodeFreqs` after creating the cfgs.
std::unique_ptr<ProtoProgramCfg> BuildFromCfgProto(
    const ProgramCfgPb &program_cfg_pb) {
  absl::flat_hash_map<int, std::unique_ptr<ControlFlowGraph>> cfgs;
  // When we construct Symbols/CFGs from protobuf, bump_ptr_allocator_ and
  // string_saver_ are used to keep all the string content. (Whereas in case of
  // constructing from binary files, the strings are kept in
  // binary_file_content.)
  auto bump_ptr_allocator = std::make_unique<llvm::BumpPtrAllocator>();
  auto string_saver = std::make_unique<llvm::StringSaver>(*bump_ptr_allocator);
  absl::flat_hash_map<InterCfgId, CFGNode *> id_to_node_map;
  // Now construct the CFG.
  for (const auto &cfg_pb : program_cfg_pb.cfg()) {
    llvm::SmallVector<llvm::StringRef, 3> names;
    names.reserve(cfg_pb.name().size());
    for (const auto &name : cfg_pb.name())
      names.emplace_back(string_saver->save(name));
    std::vector<std::unique_ptr<CFGNode>> nodes;

    for (const auto &nodepb : cfg_pb.node()) {
      std::unique_ptr<CFGNode> node =
          CreateNodeFromNodePb(cfg_pb.function_index(), nodepb);
      id_to_node_map.try_emplace(node->inter_cfg_id(), node.get());
      nodes.push_back(std::move(node));
    }
    CHECK(cfgs.emplace(cfg_pb.function_index(),
                       std::make_unique<ControlFlowGraph>(
                           cfg_pb.section_name(), cfg_pb.function_index(),
                           std::nullopt, std::move(names), std::move(nodes)))
              .second);
  }

  // Now construct the edges
  for (const auto &cfg_pb : program_cfg_pb.cfg()) {
    for (const auto &nodepb : cfg_pb.node()) {
      for (const auto &edgepb : nodepb.out_edges()) {
        auto source_id = InterCfgId{static_cast<int>(cfg_pb.function_index()),
                                    {static_cast<int>(nodepb.bb_id()), 0}};
        auto sink_id = ConvertFromPb(edgepb.sink());
        auto *from_n = id_to_node_map.at(source_id);
        auto *to_n = id_to_node_map.at(sink_id);
        CHECK_NE(from_n, nullptr);
        CHECK_NE(to_n, nullptr);
        bool inter_section =
            cfgs.at(edgepb.sink().function_index())->section_name() !=
            cfg_pb.section_name();
        cfgs.at(cfg_pb.function_index())
            ->CreateEdge(from_n, to_n, edgepb.weight(),
                         ConvertFromPb(edgepb.kind()), inter_section);
      }
    }
  }
  return std::make_unique<ProtoProgramCfg>(std::move(bump_ptr_allocator),
                                           std::move(cfgs));
}
}  // namespace

absl::StatusOr<std::unique_ptr<ProtoProgramCfg>> BuildFromCfgProtoPath(
    const std::string &path_to_cfg_proto) {
  int fd = open(path_to_cfg_proto.c_str(), O_RDONLY);
  if (fd == -1) {
    return absl::Status(absl::ErrnoToStatusCode(errno),
                        absl::StrFormat("Failed to open and read profile '%s'.",
                                        path_to_cfg_proto));
  }
  google::protobuf::io::FileInputStream fis(fd);
  fis.SetCloseOnDelete(true);
  LOG(INFO) << "Reading from '" << path_to_cfg_proto << "'.";
  ProgramCfgPb program_cfg_pb;
  if (!google::protobuf::TextFormat::Parse(&fis, &program_cfg_pb)) {
    return absl::InternalError(
        absl::StrFormat("Unable to parse profile '%s'", path_to_cfg_proto));
  }
  return BuildFromCfgProto(std::move(program_cfg_pb));
}

// Creates a whole program cfg from a `MultiCfgArg`. Calls `CalculateNodeFreqs`
// on every cfg.
std::unique_ptr<ProgramCfg> BuildFromCfgArg(MultiCfgArg multi_cfg_arg) {
  absl::flat_hash_map<int, std::unique_ptr<ControlFlowGraph>> cfgs =
      TestCfgBuilder(std::move(multi_cfg_arg)).Build();
  return std::make_unique<ProgramCfg>(std::move(cfgs));
}
}  // namespace propeller
