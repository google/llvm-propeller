#include "PLO.h"

#include <stdlib.h>

#include <atomic>
#include <fstream>
#include <string>
#include <vector>

#include "InputFiles.h"
#include "PLOELFView.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Optional.h"
#include "llvm/Support/Parallel.h"

using llvm::StringRef;

using std::vector;
using std::string;

namespace lld {
namespace plo {

PLO Plo;

bool PLO::Init(StringRef &Symfile, StringRef &Profile) {
  return InitSymfile(Symfile) && InitProfile(Profile);
}

bool PLO::InitSymfile(StringRef &Symfile) {
  auto T = lld::elf::readFile(Symfile);
  if (!T.hasValue()) return false;
  StringRef SR = T.getValue().getBuffer();
  auto DoSplit = [](StringRef &Str, StringRef &L) {
                      auto P = Str.split('\n');
                      L = P.first, Str = P.second;
                      return Str.empty() || L.empty();
                    };
  auto DoLine = [this](StringRef &L) {
                  auto S = L.split(' ');
                  if (S.first.empty()) return false;
                  uint64_t Addr = strtoull(S.first.data(), nullptr, 16);
                  StringRef Name = S.second.split(' ').second;
                  if (!Name.empty()) {
                    this->SymAddrMap[Name] = Addr;
                    return true;
                  }
                  return false;
                };
  StringRef L;
  while (!DoSplit(SR, L))
    DoLine(L);
  DoLine(L);
  fprintf(stderr, "Processed %lu entries.\n", SymAddrMap.size());
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

void PLO::ProcessFiles(vector<elf::InputFile *> &Files) {
  for (auto &Inf : Files) {
    ELFView *View = ELFView::Create(Inf->MB);
    if (View && View->Init()) {
      this->Views.emplace_back(View);
      View->BuildCfgs();
      for (auto &V : View->Cfgs) {
	this->GlobalCfgs[V->EntryNode->Address] = V.get();
      }
    }
  }

  auto FindCfg = [this](uint64_t Addr) -> ELFCfg * {
		   auto T = this->GlobalCfgs.upper_bound(Addr);
		   if (T != this->GlobalCfgs.end() && T != GlobalCfgs.begin()) {
		     return (--T)->second;
		   }
		   return nullptr;
		 };

  int A = 0, B = 0;
  for (auto  &Record : Profile.LBRs) {
    for (auto &Entry: Record->Entries) {
      ++A;
      ELFCfg *C1 = FindCfg(Entry->From);
      ELFCfg *C2 = FindCfg(Entry->To);
      if (C1 && C2) {
	++B;
	// fprintf(stderr, "Found: 0x%lx -> 0x%lx\n", Entry->From, Entry->To);
      }
    }
  }
  fprintf(stderr, "Total %d out of %d mapped to Cfg\n", B, A);
  
  // llvm::parallel::for_each(llvm::parallel::parallel_execution_policy(),
  //                            this->Views.begin(),
  //                            this->Views.end(),
  //                            [](list<unique_ptr<ELFView>>::iterator I) {
  // 			       I->get()->BuildCfgs();
  //                            });
  fprintf(stderr, "Valid   cfgs: %u\n", ValidCfgs.load());
  fprintf(stderr, "Invalid cfgs: %u\n", InvalidCfgs.load());
  fprintf(stderr, "Total BBs: %u\n", TotalBB.load());
  fprintf(stderr, "Total BB w/out address: %u\n", TotalBBWoutAddr.load());

  // Dispose the huge SymAddrMap.
  FreeContainer(SymAddrMap);
}

}  // namespace plo
}  // namespace lld
