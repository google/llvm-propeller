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
#include "PLOELFCfg.h"
#include "PLOELFView.h"
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
		      // fprintf(stderr, "Insert %s into 0x%lx\n", Name.str().c_str(), Addr);
		      this->AddrSymMap[Addr].emplace_back(Name);
		      // if (this->AddrSymMap[Addr].size() > 3) {
		      //   fprintf(stderr, "0x%lx contains more than 3 symbols.\n", Addr);
		      // }
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

static bool
IsBBSymbol(const StringRef &SymName, StringRef &FuncName) {
  FuncName = "";
  auto R = SymName.split(".bb.");
  if (R.second.empty()) return false;
  for (const char *I = R.second.data(), *J = R.second.data() + R.second.size(); I != J; ++I)
    if (*I < '0' || *I > '9') return false;
  FuncName = R.first;
  return true;
}


bool PLO::SymContainsAddr(const StringRef &SymName,
			  uint64_t SymAddr,
			  uint64_t Addr,
			  StringRef &FuncName) {
  if (!IsBBSymbol(SymName, FuncName)) {
    FuncName = SymName;
  }
  auto PairI = SymAddrSizeMap.find(FuncName);
  if (PairI != SymAddrSizeMap.end()) {
    uint64_t FuncAddr = PairI->second.first;
    uint64_t FuncSize = PairI->second.second;
    if (FuncSize > 0 && FuncAddr <= Addr && Addr < FuncAddr + FuncSize) {
      return true;
    }
  }
  FuncName = "";
  return false;
}

static uint64_t TotalCfgFound = 0;
static uint64_t TotalCfgNotFound = 0;

// TODO(shenhan): cache some results to speed up.
bool PLO::FindCfgForAddress(uint64_t Addr,
			    ELFCfg *&ResultCfg,
			    ELFCfgNode *&ResultNode) {
  ResultCfg = nullptr, ResultNode = nullptr;
  auto T = AddrSymMap.upper_bound(Addr);  // first element > Addr.
  if (T == AddrSymMap.begin())
    return false;
  auto T0 = std::prev(T);
  uint64_t SymAddr = T0->first;
  // There are multiple symbols registered on the same address.
  for (StringRef &SymName: T0->second) {
    StringRef IndexName;
    if (SymContainsAddr(SymName, SymAddr, Addr, IndexName)) {
      auto CfgLI = CfgMap.find(IndexName);
      if (CfgLI != CfgMap.end()) {
	// There might be multiple object files that define SymName.
	// So for "funcFoo.bb.3", we return Obj2.
	// For "funcFoo.bb.1", we return Obj1 (the first matching obj).
	// Obj1:
	//    Cfg1: funcFoo
	//          funcFoo.bb.1
	//          funcFoo.bb.2
	// Obj2:
	//    Cfg1: funcFoo
	//          funcFoo.bb.1
	//          funcFoo.bb.2
	//          funcFoo.bb.3
	// Also not, Objects (CfgLI->second) are sorted in the way
	// they appear on the command line, which is the same as how
	// linker chooses the weak symbol definition.
	for (auto &View: CfgLI->second) {
	  ELFCfg *Cfg = View->Cfgs[IndexName].get();
	  assert(Cfg);
	  // Check Cfg does have name "SymName".
          for (auto &N: Cfg->Nodes) {
            if (N->ShName == SymName) {
              ++TotalCfgFound;
              ResultCfg = Cfg;
              ResultNode = N.get();
              return true;
            }
          }
	}
      }
    }
  }
  ++TotalCfgNotFound;
  return false;
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
    // ProcessFile(*FileOrdinalPairs.rbegin());
  }
  llvm::parallel::for_each(llvm::parallel::parallel_execution_policy(),
                           FileOrdinalPairs.begin(),
                           FileOrdinalPairs.end(),
                           std::bind(&PLO::ProcessFile, this, _1));

  if (PLOProfile(*this).ProcessProfile(ProfileName)) {
    for (auto &View: Views) {
      for (auto &I: View->Cfgs) {
        ELFCfg *Cfg = I.second.get();
        std::cout << *Cfg;
      }
    }
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
