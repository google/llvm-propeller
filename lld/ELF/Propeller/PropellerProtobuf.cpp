#ifdef PROPELLER_PROTOBUF
#include "PropellerProtobuf.h"

#include "PropellerCFG.h"
#include "propeller_cfg.pb.h"

#include "google/protobuf/io/zero_copy_stream_impl.h"
#include "google/protobuf/text_format.h"

#include <list>

namespace lld {
namespace propeller {

void ProtobufPrinter::printCFGGroup() {
  if (!google::protobuf::TextFormat::Print(cfgGroupPb, &outStream))
    error("Failed to dump cfg to file.");
  llvm::outs() << "Printed " << cfgGroupPb.cfg_list_size() << " cfgs to '"
               << outName << "'.\n";
}

void ProtobufPrinter::addCFG(ControlFlowGraph &cfg,
                             std::list<CFGNode *> *orderedBBs) {
  llvm::plo::cfg::CFG &cfgpb = *(cfgGroupPb.add_cfg_list());
  cfgpb.set_name(cfg.name.str());
  cfgpb.set_size(cfg.size);
  cfgpb.set_object_name(cfg.view->viewName.str());

  auto populateEdgePb = [](llvm::plo::cfg::Edge &edgepb, CFGEdge &edge) {
    edgepb.set_source(edge.src->getBBIndex());
    edgepb.set_target(edge.sink->getBBIndex());
    edgepb.set_profile_count(edge.weight);
    edgepb.set_type(static_cast<llvm::plo::cfg::Edge_Type>(
        (int)(edge.type) - (int)(CFGEdge::INTRA_FUNC)));
  };

  auto populateBasicBlockPb =
      [&populateEdgePb](llvm::plo::cfg::BasicBlock &bbpb, CFGNode &node) {
        bbpb.set_index(node.getBBIndex());
        bbpb.set_size(node.shSize);
        bbpb.set_profile_count(node.freq);

        for (CFGEdge *e : node.ins)
          populateEdgePb(*(bbpb.add_incoming_edges()), *e);

        for (CFGEdge *e : node.outs)
          populateEdgePb(*(bbpb.add_outgoing_edges()), *e);

        if (node.ftEdge)
          populateEdgePb(*(bbpb.mutable_fallthrough()), *node.ftEdge);

        bbpb.set_hot_tag(node.hotTag);
      };

  if (orderedBBs)
    for (auto *node : *orderedBBs)
      populateBasicBlockPb(*(cfgpb.add_basic_blocks()), *node);
  else
    for (auto &node : cfg.nodes)
      populateBasicBlockPb(*(cfgpb.add_basic_blocks()), *node);

  CFGNode *entryNode = cfg.getEntryNode();
  cfgpb.set_entry_block(entryNode ? entryNode->getBBIndex() : 0);
}

} // namespace propeller
} // namespace lld
#endif
