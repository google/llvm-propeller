#include "PLO.h"

#include <stdlib.h>

#include <atomic>
#include <fstream>
#include <functional>
#include <iostream>
#include <list>
#include <mutex>
#include <numeric>
#include <string>
#include <vector>

#include "Config.h"
#include "InputFiles.h"
#include "PLOBBOrdering.h"
#include "PLOBBReordering.h"
#include "PLOELFCfg.h"
#include "PLOELFView.h"
#include "PLOFuncOrdering.h"
#include "PLOProfile.h"
#include "Symbols.h"
#include "SymbolTable.h"

#include "lld/Common/Memory.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Optional.h"
#include "llvm/Support/Parallel.h"

using llvm::StringRef;

using std::list;
using std::mutex;
using std::pair;
using std::placeholders::_1;
using std::vector;
using std::string;

using lld::elf::Config;

namespace lld {
namespace plo {

PLO::PLO(lld::elf::SymbolTable *ST) : Symtab{ST} {}
PLO::~PLO() {}

Symfile::Symfile() : BPAllocator(), SymStrSaver(BPAllocator) {}
Symfile::~Symfile() { BPAllocator.Reset(); }

bool Symfile::init(StringRef SymfileName) {
  std::ifstream fin(SymfileName.str());
  if (!fin.good()) return false;
  string line;
  while (fin.good() && !std::getline(fin, line).eof()) {
    StringRef L(line);
    auto S1 = L.split(' ');
    if (S1.first.empty()) continue;
    uint64_t Addr = strtoull(S1.first.data(), nullptr, 16);
    auto S2 = S1.second.split(' ');
    if (S2.first.empty()) continue;
    uint64_t Size = strtoull(S2.first.data(), nullptr, 16);
    auto S3 = S2.second.split(' ');
    char Type = *(S3.first.data());
    if (Type == 'T' || Type == 't' || Type == 'W' || Type == 'w') {
      StringRef TemporalName = S3.second;
      if (!TemporalName.empty()) {
        StringRef NameRef = this->SymStrSaver.save(TemporalName);
        auto SymHandler = SymList.emplace(SymList.end(), NameRef, Addr, Size);
        NameMap.emplace(NameRef, SymHandler);
        AddrMap[Addr].emplace_back(SymHandler);
      }
    }
  }
  return true;
}

bool PLO::dumpCfgsToFile() const {
  if (Config->CfgDump.empty())
    return false;

  std::ofstream OS;
  OS.open(Config->CfgDump.str(), std::ios::out);

  if (!OS.good()) {
    fprintf(stderr, "File is not good for writing: <%s>\n",
            Config->CfgDump.str().c_str());
    return false;
  }

  for (auto &P : CfgMap) {
    (*P.second.begin())->dumpToOS(OS);
  }

  OS.close();
  return true;
}

// This method is thread safe.
void PLO::processFile(const pair<elf::InputFile *, uint32_t> &Pair) {
  auto *Inf = Pair.first;
  ELFView *View = ELFView::create(Inf->getName(), Pair.second, Inf->MB);
  if (View) {
    ELFCfgBuilder(*this, View).buildCfgs();
    {
      // Updating global data structure.
      std::lock_guard<mutex> L(this->Lock);
      this->Views.emplace_back(View);
      for (auto &P : View->Cfgs) {
        auto R = CfgMap[P.first].emplace(P.second.get());
        (void)(R);
        assert(R.second);
      }
    }
  }
}

void PLO::resetEntryNodeSizes() {
  for (auto &P : CfgMap) {
    auto &Cfg = *P.second.begin();
    auto FirstNode = Cfg->Nodes.begin();
    if(FirstNode==Cfg->Nodes.end())
      continue;
    auto SecondNode = std::next(FirstNode);

    while(SecondNode!=Cfg->Nodes.end()){
      (*FirstNode)->ShSize = (*SecondNode)->MappedAddr - (*FirstNode)->MappedAddr;
      FirstNode++;
      SecondNode++;
    }
  }
}

void PLO::calculateNodeFreqs() {
  auto sumEdgeWeights = [](list<ELFCfgEdge *> &Edges) -> uint64_t {
    return std::accumulate(Edges.begin(), Edges.end(), 0,
                           [](uint64_t PSum, const ELFCfgEdge *Edge) {
                             return PSum + Edge->Weight;
                           });
  };
  for (auto &P : CfgMap) {
    auto &Cfg = *P.second.begin();
    if (Cfg->Nodes.empty())
      continue;
    bool Hot = false;
    Cfg->forEachNodeRef([&Hot, &sumEdgeWeights](ELFCfgNode &Node) {
      uint64_t MaxCallOut =
          Node.CallOuts.empty() ?
          0 :
          (*std::max_element(Node.CallOuts.begin(), Node.CallOuts.end(),
                          [](const ELFCfgEdge *E1, const ELFCfgEdge *E2){
                            return E1->Weight < E2->Weight;
                          }))->Weight;
      Node.Freq =
          std::max({sumEdgeWeights(Node.Outs), sumEdgeWeights(Node.Ins),
                    sumEdgeWeights(Node.CallIns), MaxCallOut});
      Hot |= (Node.Freq != 0);
    });
    if (Hot && Cfg->getEntryNode()->Freq == 0)
      Cfg->getEntryNode()->Freq = 1;
  }
}

bool PLO::processFiles(vector<elf::InputFile *> &Files){
  if (!Syms.init(Config->SymFile))
    return false;
    
  vector<pair<elf::InputFile *, uint32_t>> FileOrdinalPairs;
  int Ordinal = 0;
  for (auto &F : Files) {
    FileOrdinalPairs.emplace_back(F, ++Ordinal);
  }
  llvm::parallel::for_each(llvm::parallel::parallel_execution_policy(),
                           FileOrdinalPairs.begin(),
                           FileOrdinalPairs.end(),
                           std::bind(&PLO::processFile, this, _1));
  if (PLOProfile(*this).process(Config->Profile)) {
    //resetEntryNodeSizes();
    calculateNodeFreqs();
    dumpCfgsToFile();
    Syms.reset();
    return true;
  }
  return false;
}

vector<StringRef> PLO::genSymbolOrderingFile() {
  list<const ELFCfg *> CfgOrder;

  if(Config->ReorderFunctions){
    CfgOrder = PLOFuncOrdering<CCubeAlgorithm>(*this).doOrder();
  }else{
    std::vector<ELFCfg*> OrderResult;
    ForEachCfgRef([&OrderResult](ELFCfg& Cfg){OrderResult.push_back(&Cfg);});

    std::sort(OrderResult.begin(), OrderResult.end(),
              [](const ELFCfg* A, const ELFCfg* B) {
                const auto &AEntry = A->getEntryNode();
                const auto &BEntry = B->getEntryNode();
                return AEntry->MappedAddr < BEntry->MappedAddr;
              });
    std::copy(OrderResult.begin(), OrderResult.end(), std::back_inserter(CfgOrder));
  }

  list<StringRef> SymbolList(1, "Hot");
  const auto HotPlaceHolder = SymbolList.begin();
  const auto ColdPlaceHolder = SymbolList.end();
  for (auto *Cfg : CfgOrder) {
    if (Cfg->isHot() && Config->ReorderBlocks){
      ExtTSPChainBuilder(Cfg).doSplitOrder(SymbolList, HotPlaceHolder, Config->SplitFunctions ? ColdPlaceHolder : HotPlaceHolder);
    } else {
      Cfg->forEachNodeRefConst(
        [&SymbolList, ColdPlaceHolder](ELFCfgNode &N) {
          SymbolList.insert(ColdPlaceHolder, N.ShName);
        });
    }
  }
  SymbolList.erase(HotPlaceHolder);
  return vector<StringRef>(
      std::move_iterator<list<StringRef>::iterator>(SymbolList.begin()),
      std::move_iterator<list<StringRef>::iterator>(SymbolList.end()));
}

}  // namespace plo
}  // namespace lld

