#include "PLO.h"

#include <stdlib.h>

#include <atomic>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "InputFiles.h"
#include "PLOELFView.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Optional.h"
#include "llvm/Support/Parallel.h"

using llvm::StringRef;

using std::mutex;
using std::placeholders::_1;
using std::vector;
using std::string;

namespace lld {
namespace plo {

PLO Plo;

bool PLO::Init(StringRef &Symfile, StringRef &Profile) {
  return InitSymfile(Symfile) && InitProfile(Profile);
}

// Each line contains: Addr Size Type Name
bool PLO::InitSymfile(StringRef &Symfile) {
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
		      if (this->AddrSymMap[Addr].size() > 3) {
			fprintf(stderr, "0x%lx contains more than 3 symbols.\n", Addr);
		      }
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

LBREntry *LBREntry::CreateEntry(const StringRef &SR) {
  unique_ptr<LBREntry> EP(new LBREntry());
  auto L1 = SR.split('/');
  // Passing "0" as radix enables autosensing, so no need to skip "0x"
  // prefix.
  if (L1.first.empty() || L1.first.getAsInteger(0, EP->From))
    return nullptr;
  auto L2 = L1.second.split('/');
  if (L2.first.empty() || L2.first.getAsInteger(0, EP->To))
    return nullptr;
  auto L3 = L2.second.split('/');
  if (L3.first.empty() || L3.second.empty())
    return nullptr;
  EP->Predict = *(L3.first.data());
  if (EP->Predict != 'M' && EP->Predict != 'P' && EP->Predict != '-')
    return nullptr;
  if (L3.second.rsplit('/').second.getAsInteger(10, EP->Cycles))
    return nullptr;
  // fprintf(stderr, "Created entry: 0x%lx->0x%lx %c %d\n",
  // 	  EP->From, EP->To, EP->Predict, EP->Cycles);
  return EP.release();
}

bool PLO::InitProfile(StringRef &Profile) {
  // Profile is huge, don't read all of the file into the memory.
  std::ifstream fin(Profile.str());
  if (!fin.good()) return false;
  string line;
  while (fin.good() && !std::getline(fin, line).eof()) {
    if (line.empty()) continue;
    unique_ptr<LBRRecord> Rec(new LBRRecord());
    const char *p = line.c_str();
    const char *q = p + 1;
    do {
      while (*(q++) != ' ');
      LBREntry *Entry = LBREntry::CreateEntry(StringRef(p, q - p - 1));
      if (Entry == nullptr) {
	fprintf(stderr, "Invalid entry: %s\n", StringRef(p, q - p - 1).str().c_str());
	break;
      }
      Rec->Entries.emplace_back(Entry);
      if (*q == '\0') break;
      p = q + 1;
      q = p + 1;
    } while(true);
    if (!Rec->Entries.empty()) {
      this->Profile.LBRs.emplace_back(Rec.release());
    }
  }
  fprintf(stderr, "Total LBR records created: %lu\n", this->Profile.LBRs.size());
  return true;
}

// This method if thread safe.
void PLO::ProcessFile(elf::InputFile *Inf) {
  ELFView *View = ELFView::Create(Inf->getName(), Inf->MB);
  if (View && View->Init()) {
    // fprintf(stderr, "Building Cfgs for %s...\n", Inf->getName().str().c_str());
    View->BuildCfgs();
    // Updating global data structure.
    {
      std::lock_guard<mutex> L(this->Lock);
      this->Views.emplace_back(View);
      for (auto &P : View->Cfgs) {
	auto &CfgList = CfgMap[P.first];
	CfgList.emplace_back(View);
      }
    }
  }
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


static bool
SymContainsAddr(const StringRef &SymName, uint64_t SymAddr, uint64_t Addr, StringRef &FuncName) {
  if (IsBBSymbol(SymName, FuncName)) {
    ;
  } else {
    FuncName = SymName;
  }
  auto PairI = Plo.SymAddrSizeMap.find(FuncName);
  if (PairI != Plo.SymAddrSizeMap.end()) {
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
static bool
FindCfgForAddress(uint64_t Addr, ELFCfg *&ResultCfg, ELFCfgNode *&ResultNode) {
  ResultCfg = nullptr, ResultNode = nullptr;
  auto T = Plo.AddrSymMap.upper_bound(Addr);  // first element > Addr.
  if (T == Plo.AddrSymMap.begin())
    return false;
  auto T0 = std::prev(T);
  uint64_t SymAddr = T0->first;
  // There are multiple symbols registered on the same address.
  for (StringRef &SymName: T0->second) {
    StringRef IndexName;
    if (SymContainsAddr(SymName, SymAddr, Addr, IndexName)) {
      auto CfgLI = Plo.CfgMap.find(IndexName);
      if (CfgLI != Plo.CfgMap.end()) {
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
	for (auto &View: CfgLI->second) {
	  ELFCfg *Cfg = View->Cfgs[IndexName].get();
	  assert(Cfg);
	  // Check Cfg does have name "SymName".
	  for (auto &N : Cfg->Nodes) {
	    if (N.second->ShName == SymName) {
	      ++TotalCfgFound;
	      ResultCfg = Cfg;
	      ResultNode = N.second.get();
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

static void PrintLBRRecord(LBRRecord *Record) {
  fprintf(stderr, "================ Start of LBRecord ================\n");
  for (auto P = Record->Entries.rbegin(), Q = Record->Entries.rend();
   	 P != Q; ++P) {
    auto &Entry = *P;
    uint64_t From = Entry->From, To = Entry->To;
    ELFCfg *FromCfg, *ToCfg;
    ELFCfgNode *FromNode, *ToNode;
    FindCfgForAddress(From, FromCfg, FromNode);
    FindCfgForAddress(To, ToCfg, ToNode);
    fprintf(stderr, "  %s(0x%lx) -> %s(0x%lx)\n",
	    FromNode ? FromNode->ShName.str().c_str() : "NA",
	    From,
	    ToNode ? ToNode->ShName.str().c_str() : "NA",
	    To);
  }
  fprintf(stderr, "================ End of LBRecord ================\n");
}

void PLO::ProcessLBRs() {
  int Total = 0;
  uint32_t Mapped = 0;
  uint32_t Unmapped = 0;
  uint32_t Strange = 0;
  uint64_t LastFromAddr{0}, LastToAddr{0};
  for (auto  &Record : Profile.LBRs) {
     Total += Record->Entries.size();
     ELFCfg *LastToCfg{nullptr};
     ELFCfgNode *LastToNode{nullptr};

     // LBR records looks like:
     //    Entry0: LastFromCfg[LastFromNode] -> LastToCfg[LastToNode] |
     //    Entry1: FromCfg[FromNode] -> ToCfg[ToNode]
     for (auto P = Record->Entries.rbegin(), Q = Record->Entries.rend();
   	 P != Q; ++P) {
       auto &Entry = *P;
       uint64_t From = Entry->From, To = Entry->To;
       ELFCfg *FromCfg, *ToCfg;
       ELFCfgNode *FromNode, *ToNode;
       FindCfgForAddress(From, FromCfg, FromNode);
       FindCfgForAddress(To, ToCfg, ToNode);

       if (FromCfg && FromCfg == ToCfg) {
	 FromCfg->MapBranch(FromNode, ToNode);
       }
       // Mark everything between LastToCfg[LastToNode] and FromCfg[FromNode].
       if (LastToCfg == FromCfg) {
	 if (LastToCfg->MarkPath(LastToNode, FromNode) == false) {
	   if (!(LastFromAddr == From && LastToAddr == To && std::next(P) == Q)) {
	     ++Strange;
	     PrintLBRRecord(Record.get());
	     LastToCfg->Diagnose();
	   }
	 }
       }
       LastToCfg = ToCfg;
       LastToNode = ToNode;
       LastFromAddr = From;
       LastToAddr = To;
     }
  }

  fprintf(stderr, "Total strange: %d\n", Strange);

  fprintf(stderr, "Total %d branches mapped, %d branches unmapped.\n",
	  Mapped, Unmapped);

  fprintf(stderr, "Cfg found: %lu, not found: %lu.\n",
	  TotalCfgFound,
	  TotalCfgNotFound);
  // _ZN4llvm9AAResults13getModRefInfoEPKNS_11InstructionERKNS_8OptionalINS_14MemoryLocationEEE
  // for (auto &View: Plo.Views) {
  //   for (auto &I: View->Cfgs) {
  //     ELFCfg *Cfg = I.second.get();
  //     Cfg->Diagnose();
  //   }
  // }
       // auto C1 = FindObjectInMap(GlobalCfgs, Entry->From);
       // auto C2 = FindObjectInMap(GlobalCfgs, Entry->To);
  //     if (C1 != GlobalCfgs.end() && C1 == C2) {
  // 	// intra block jumps
  // 	auto *Cfg = C1->second;
  // 	auto N1 = FindObjectInMap(Cfg->Nodes, Entry->From);
  // 	auto N2 = FindObjectInMap(Cfg->Nodes, Entry->To);
  // 	if (N1 != Cfg->Nodes.end() && N2 != Cfg->Nodes.end()) {
  // 	  ++TotalProcessedEntries;
  // 	} else {
  // 	  fprintf(stderr, "LBR: 0x%lx(%s)->0x%lx(%s) not found\n",
  // 		  Entry->From, N1 == Cfg->Nodes.end() ? "NF" : "F",
  // 		  Entry->To, N2 == Cfg->Nodes.end() ? "NF": "F");
  // 	  fprintf(stderr, "Cfg: 0x%lx:%s\n", Cfg->GetAddress(), Cfg->Name.str().c_str());
  // 	  Cfg->Diagnose();
  // 	  std::next(C1)->second->Diagnose();
  // 	  exit(1);
  // 	}
  //     }
  //   }
  // }
  // fprintf(stderr, "Processed %d out of %d LBR entries.\n", TotalProcessedEntries, Total);
}
    

void PLO::ProcessFiles(vector<elf::InputFile *> &Files) {
  llvm::parallel::for_each(llvm::parallel::parallel_execution_policy(),
                           Files.begin(),
                           Files.end(),
			   std::bind(&PLO::ProcessFile, this, _1));


  // _ZN4llvm9AAResults13getModRefInfoEPKNS_11InstructionERKNS_8OptionalINS_14MemoryLocationEEE
  auto &T = CfgMap["_ZN4llvm9AAResults13getModRefInfoEPKNS_11InstructionERKNS_8OptionalINS_14MemoryLocationEEE"];
  for (auto *View: T) {
    fprintf(stderr, "View: %s\n", View->ViewName.str().c_str());
  }
  exit(0);

  // FreeContainer(SymAddrMap);  // No longer needed after we create Cfg.
  // FreeContainer(SymSizeMap);
  ProcessLBRs();
  // fprintf(stderr, "Finsihed creating cfgs.\n");

  // fprintf(stderr, "Total %d intra procedure jumps.\n", B);
  
  // fprintf(stderr, "Valid   cfgs: %u\n", ValidCfgs.load());
  // fprintf(stderr, "Invalid cfgs: %u\n", InvalidCfgs.load());
  // fprintf(stderr, "Total BBs: %u\n", TotalBB.load());
  // fprintf(stderr, "Total BB w/out address: %u\n", TotalBBWoutAddr.load());

  // Dispose the huge SymAddrMap.
}

}  // namespace plo
}  // namespace lld
