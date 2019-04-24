#include <fstream>
#include <string>
#include <iostream>
#include <stdio.h>
#include <set>
#include <deque>
#include <list>

#include "../../ELF/PLOELFView.h"
#include "../../ELF/PLOELFCfg.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CommandLine.h"
#include "lld/Common/Args.h"

using llvm::StringRef;

using std::list;
using std::set;
using std::string;
using std::unique_ptr;
using namespace llvm;

namespace opts{

static cl::opt<std::string>
CfgRead(
  "cfg-read",
  cl::desc("File to read the Cfg from."),
  cl::Required);

static cl::opt<std::string>
CfgDump(
  "cfg-dump",
  cl::desc("File to dump the cfg to."),
  cl::ZeroOrMore);

static cl::opt<std::string>
LayoutDump(
  "layout-dump",
  cl::desc("File to dump the layout to."),
  cl::ZeroOrMore);
}


namespace lld{
namespace plo {

class NodeChain {
 public:
  list<const ELFCfgNode *> Nodes;
  uint32_t Size{0};
  uint64_t  Freq{0};

  NodeChain(const ELFCfgNode * Node){
    Nodes.push_back(Node);
    Size = Node->ShSize;
    Freq = Node->Freq;
  }
};

class NodeChainBuilder {
 public:
  const ELFCfg * Cfg;
  set<NodeChain *> Chains;
  std::map<const ELFCfgNode *, NodeChain *> NodeToChainMap;
  std::map<const ELFCfgNode *, uint32_t> NodeOffset;
  std::list<const ELFCfgNode *> Layout;

  NodeChainBuilder(const ELFCfg * _Cfg): Cfg(_Cfg){
    for(auto& I: Cfg->Nodes){
      auto* Node = I.second.get();
      auto* Chain = new NodeChain(Node);
      NodeToChainMap[Node] = Chain;
      Chains.insert(Chain);
      NodeOffset[Node] = 0;
    }
  }

  void GreedyChain(){
    std::deque<const ELFCfgEdge*> CfgEdgeQ;

    for(auto& Edge: Cfg->IntraEdges)
      CfgEdgeQ.push_back(Edge.get());

    std::sort(CfgEdgeQ.begin(), CfgEdgeQ.end(),
              [](const ELFCfgEdge * Edge1, const ELFCfgEdge * Edge2){
                if (Edge1->Weight == Edge2->Weight){
                  if (Edge1->Src->MappedAddr == Edge2->Src->MappedAddr)
                    return Edge1->Sink->MappedAddr > Edge2->Sink->MappedAddr;
                  return Edge1->Src->MappedAddr > Edge2->Src->MappedAddr;
                }
                return Edge1->Weight < Edge2->Weight;
              });
    while(!CfgEdgeQ.empty()){
      auto * Edge = CfgEdgeQ.back();
      CfgEdgeQ.pop_back();
      if (AttachNodes(Edge->Src, Edge->Sink))
        std::cerr << "Merging chains: " << Edge->Src->ShName.str() << " --> " << Edge->Sink->ShName.str() << " : " << Edge->Weight << "\n";
      else
        std::cerr << "Could not merge: " <<  Edge->Src->ShName.str() << " --> " << Edge->Sink->ShName.str() << " : " << Edge->Weight << "\n";
    }
  }

  void ChainSort(){
    std::vector<const NodeChain*> ChainsCopy(Chains.begin(), Chains.end());
    std::sort(ChainsCopy.begin(), ChainsCopy.end(),
              [](const NodeChain* Chain1, const NodeChain* Chain2){
                double Chain1ExecDensity = (double)Chain1->Freq / Chain1->Size;
                double Chain2ExecDensity = (double)Chain2->Freq / Chain2->Size;
                if(Chain1ExecDensity == Chain2ExecDensity)
                  return Chain1->Nodes.front()->MappedAddr < Chain2->Nodes.front()->MappedAddr;
                return Chain1ExecDensity > Chain2ExecDensity;
              });
    for(auto * Chain: ChainsCopy)
      for(auto * Node: Chain->Nodes)
        Layout.push_back(Node);
  }

  void MergeChains(NodeChain* LeftChain, NodeChain* RightChain){
    for(auto* Node: RightChain->Nodes){
      LeftChain->Nodes.push_back(Node);
      NodeToChainMap[Node]=LeftChain;
      NodeOffset[Node] += LeftChain->Size;
    }
    LeftChain->Size += RightChain->Size;
    LeftChain->Freq += RightChain->Freq;
    Chains.erase(RightChain);
  }

  bool AttachNodes(ELFCfgNode * Src, ELFCfgNode * Sink){
    if(Src->Freq==0 ^ Sink->Freq==0)
      return false;
    auto& SrcChain = NodeToChainMap[Src];
    auto& SinkChain = NodeToChainMap[Sink];
    if(SrcChain == SinkChain)
      return false;
    if(SrcChain->Nodes.back()!=Src || SinkChain->Nodes.front()!=Sink)
      return false;
    MergeChains(SrcChain, SinkChain);
    return true;
  }
};

}
}

int main(int Argc, const char **Argv) {
  cl::ParseCommandLineOptions(Argc, Argv, "Codestitcher");
  StringRef CfgFile = opts::CfgRead.getValue();
  auto CfgReader = lld::plo::ELFCfgReader(CfgFile);
  CfgReader.ReadCfgs();
  fprintf(stderr, "Read all Cfgs\n");

  if (!opts::CfgDump.empty()){
    std::ofstream fout;
    fout.open(opts::CfgDump.getValue(), std::ios::out);

    if (!fout.good()) {
      fprintf(stderr, "File is not good for writing: <%s>\n", opts::CfgDump.getValue().c_str());
      exit(0);
    } else {
        for (auto &Cfg: CfgReader.Cfgs) {
          Cfg->WriteToFile(fout);
        }
      fout.close();
    }
  }

  if(!opts::LayoutDump.empty()){
    std::ofstream fout;
    fout.open(opts::LayoutDump.getValue(), std::ios::out);
    for(auto& Cfg: CfgReader.Cfgs){
      if (Cfg->IsHot()){
        auto ChainBuilder = lld::plo::NodeChainBuilder(Cfg.get());
        ChainBuilder.GreedyChain();
        ChainBuilder.ChainSort();
        for(auto * Node: ChainBuilder.Layout)
          fout << Node->ShName.str() << "\n";
      } else {
        for(auto& P: Cfg->Nodes){
          fout << P.second->ShName.str() << "\n";
        }
      }
    }
    fout.close();
  }
}
