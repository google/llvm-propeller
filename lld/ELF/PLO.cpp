#include "PLO.h"

#include <stdlib.h>

#include <atomic>
#include <fstream>
#include <functional>
#include <iostream>
#include <list>
#include <mutex>
#include <string>
#include <vector>

#include "InputFiles.h"
#include "PLOBBOrdering.h"
#include "PLOELFCfg.h"
#include "PLOELFView.h"
#include "PLOFuncOrdering.h"
#include "PLOProfile.h"

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

// Each line contains: Addr Size Type Name
bool PLO::ProcessSymfile(StringRef &Symfile) {
  // Note: readFile keeps the content of the file till the end of lld
  // run. So we may use MemoryBufferRef safely.
  auto T = lld::elf::readFile(Symfile);
  if (!T.hasValue()) return false;
  StringRef SR = T.getValue().getBuffer();
  auto DoSplit = [](StringRef &Str, StringRef &L) {
                      auto P = Str.split('\n');
                      L = P.first, Str = P.second;
                      return Str.empty() || L.empty();
                    };
  auto DoLine = [this](StringRef &L) {
                  auto S1 = L.split(' ');
                  if (S1.first.empty()) return false;
                  uint64_t Addr = strtoull(S1.first.data(), nullptr, 16);
		  auto S2 = S1.second.split(' ');
		  if (S2.first.empty()) return false;
		  uint64_t Size = strtoull(S2.first.data(), nullptr, 16);
		  auto S3 = S2.second.split(' ');
		  char Type = *(S3.first.data());
		  if (Type == 'T' || Type == 't' || Type == 'W' || Type == 'w') {
		    StringRef Name = S3.second;
		    if (!Name.empty()) {
		      this->AddrSymMap[Addr].emplace_back(Name);
		      this->SymAddrSizeMap.emplace(
                          std::piecewise_construct,
			  std::forward_as_tuple(Name),
			  std::forward_as_tuple(Addr, Size));
		      return true;
		    }
		  }
                  return false;
                };
  StringRef L;
  while (!DoSplit(SR, L))
    DoLine(L);
  DoLine(L);
  fprintf(stderr, "Processed %lu symfile entries.\n", AddrSymMap.size());
  return true;
}

// This method if thread safe.
void PLO::ProcessFile(const pair<elf::InputFile *, uint32_t> &Pair) {
  auto *Inf = Pair.first;
  ELFView *View = ELFView::Create(Inf->getName(), Pair.second, Inf->MB);
  if (View) {
    ELFCfgBuilder(*this, View).BuildCfgs();
    // Updating global data structure.
    {
      std::lock_guard<mutex> L(this->Lock);
      this->Views.emplace_back(View);
      for (auto &P : View->Cfgs) {
	auto R = CfgMap[P.first].emplace(View);
	(void)(R);
	assert(R.second);
      }
    }
  }
}

bool PLO::ProcessFiles(vector<elf::InputFile *> &Files,
                       StringRef &SymfileName,
                       StringRef &ProfileName) {
  if (!ProcessSymfile(SymfileName)) {
    return false;
  }
    
  vector<pair<elf::InputFile *, uint32_t>> FileOrdinalPairs;
  int Ordinal = 0;
  for (auto &F : Files) {
    FileOrdinalPairs.emplace_back(F, ++Ordinal);
  }
  llvm::parallel::for_each(llvm::parallel::parallel_execution_policy(),
                           FileOrdinalPairs.begin(),
                           FileOrdinalPairs.end(),
                           std::bind(&PLO::ProcessFile, this, _1));

  if (PLOProfile(*this).ProcessProfile(ProfileName)) {
    uint64_t TotalCfgs = 0;
    uint64_t CfgHasWeight = 0;
    PLOFuncOrdering PFO(*this);
    std::cout << PFO.CG;
    // for (auto &View: Views) {
    //   for (auto &I: View->Cfgs) {
    //     ++TotalCfgs;
    //     ELFCfg *Cfg = I.second.get();
    //     if (Cfg->Weight > 1000) {
    //       ++CfgHasWeight;
    //       std::cout << *Cfg;

    //       PLOBBOrdering(*Cfg).DoOrder();
    //     }
    //   }
    // }
    fprintf(stderr, "Cfg has Weight / Total Cfg: %lu / %lu\n", CfgHasWeight, TotalCfgs);
    return true;
  }

  return false;

  // fprintf(stderr, "Finsihed creating cfgs.\n");

  // fprintf(stderr, "Total %d intra procedure jumps.\n", B);

  // fprintf(stderr, "Valid   cfgs: %u\n", ValidCfgs.load());
  // fprintf(stderr, "Invalid cfgs: %u\n", InvalidCfgs.load());
  // fprintf(stderr, "Total BBs: %u\n", TotalBB.load());
  // fprintf(stderr, "Total BB w/out address: %u\n", TotalBBWoutAddr.load());
}

}  // namespace plo
}  // namespace lld
