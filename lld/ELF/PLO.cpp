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

#include "InputFiles.h"
#include "PLOBBOrdering.h"
#include "PLOELFCfg.h"
#include "PLOELFView.h"
#include "PLOFuncOrdering.h"
#include "PLOProfile.h"

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

namespace lld {
namespace plo {

PLO::PLO() {}
PLO::~PLO() {}

Symfile::Symfile() : BPAllocator(), SymStrSaver(BPAllocator) {}
Symfile::~Symfile() { BPAllocator.Reset(); }

bool Symfile::Init(StringRef SymfileName) {
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

bool PLO::DumpCfgsToFile(StringRef &CfgDumpFile) const {
  if(CfgDumpFile.empty())
    return false;

  std::ofstream OS;
  OS.open(CfgDumpFile.str(), std::ios::out);

  if(!OS.good()){
    fprintf(stderr, "File is not good for writing: <%s>\n", CfgDumpFile.str().c_str());
    return false;
  }

  for (auto &P : CfgMap) {
    (*P.second.begin())->DumpToOS(OS);
  }

  OS.close();
  return true;
}

// This method is thread safe.
void PLO::ProcessFile(const pair<elf::InputFile *, uint32_t> &Pair) {
  auto *Inf = Pair.first;
  fprintf(stderr, "Processing: %s\n", Inf->getName().str().c_str());
  ELFView *View = ELFView::Create(Inf->getName(), Pair.second, Inf->MB);
  if (View) {
    ELFCfgBuilder(*this, View).BuildCfgs();
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

void PLO::CalculateNodeFreqs() {
  for(auto &P: CfgMap){
    auto& Cfg = *P.second.begin();
    if (Cfg->Nodes.empty())
      return;
    bool Hot = false;
    for (auto& Node : Cfg->Nodes){
      uint64_t SumOuts = std::accumulate(Node->Outs.begin(), Node->Outs.end(), 0, [] (uint64_t PSum, const ELFCfgEdge * Edge){
        return PSum + Edge->Weight;
      });
      uint64_t SumIns = std::accumulate(Node->Ins.begin(), Node->Ins.end(), 0, [] (uint64_t PSum, const ELFCfgEdge * Edge){
        return PSum + Edge->Weight;
      });

      uint64_t SumCallIns =  std::accumulate(Node->CallIns.begin(), Node->CallIns.end(), 0, [] (uint64_t PSum, const ELFCfgEdge * Edge){
        return PSum + Edge->Weight;
      });

      Node->Freq = std::max({SumOuts, SumIns, SumCallIns});
      Hot |= (Node->Freq != 0);
    }
    if (Hot && Cfg->GetEntryNode()->Freq==0)
      Cfg->GetEntryNode()->Freq = 1;
  }
}


bool PLO::ProcessFiles(vector<elf::InputFile *> &Files,
                       StringRef &SymfileName,
                       StringRef &ProfileName,
                       StringRef &CfgDump) {
  if (!Syms.Init(SymfileName))
    return false;
    
  vector<pair<elf::InputFile *, uint32_t>> FileOrdinalPairs;
  int Ordinal = 0;
  for (auto &F : Files) {
    FileOrdinalPairs.emplace_back(F, ++Ordinal);
  }
  llvm::parallel::for_each(llvm::parallel::parallel_execution_policy(),
                           FileOrdinalPairs.begin(),
                           FileOrdinalPairs.end(),
                           std::bind(&PLO::ProcessFile, this, _1));
  if (PLOProfile(*this).Process(ProfileName)) {
    CalculateNodeFreqs();
    DumpCfgsToFile(CfgDump);
    Syms.Reset();
    return true;
  }
  return false;
}

vector<StringRef> PLO::GenSymbolOrderingFile() { 
  PLOFuncOrdering<CCubeAlgorithm> PFO(*this);
  list<ELFCfg *> OrderResult = PFO.DoOrder();
  list<StringRef> SymbolList(1, "Hot");
  const auto HotPlaceHolder = SymbolList.begin();
  const auto ColdPlaceHolder = SymbolList.end();
  for (auto *Cfg : OrderResult) {
    if (Cfg->IsHot()) {
      PLOBBOrdering(*Cfg).DoOrder(SymbolList, HotPlaceHolder, ColdPlaceHolder);
    } else {
      Cfg->ForEachNodeRef(
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

