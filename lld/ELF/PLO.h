#ifndef LLD_ELF_PLO_H
#define LLD_ELF_PLO_H

#include <atomic>
#include <list>
#include <map>
#include <mutex>
#include <set>
#include <utility>
#include <vector>

#include "PLOELFView.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/MemoryBuffer.h"

using std::atomic;
using std::list;
using std::map;
using std::mutex;
using std::pair;
using std::set;
using std::unique_ptr;
using std::vector;

using llvm::MemoryBufferRef;
using llvm::StringRef;

namespace lld {

namespace elf {
  class InputFile;
}

namespace plo {

class LBREntry {
public:
  uint64_t From;
  uint64_t To;
  int Cycles;
  char Predict;

  static LBREntry *CreateEntry(const StringRef &SR);
};

class LBRRecord {
public:

  list<unique_ptr<LBREntry>> Entries;
  
};

class PLOProfile {
public:
  list<unique_ptr<LBRRecord>> LBRs;
};

class PLO {
public:
  PLO() {}
  ~PLO() {}

  bool Init(StringRef &Symfile, StringRef &Profile);
  bool InitSymfile(StringRef &Symfile);
  bool InitProfile(StringRef &Profile);

  void ProcessFiles(vector<elf::InputFile *> &Files);
  void ProcessFile(pair<elf::InputFile *, uint32_t> &Pair);
  void ProcessLBRs();

  // Thread safety is guaranteed.
  void CreateCfgForFile(elf::InputFile *Inf);
  // Addr -> Symbol (for all 't', 'T', 'w' and 'W' symbols) map.
  map<uint64_t, llvm::SmallVector<StringRef, 3>> AddrSymMap;
  map<StringRef, pair<uint64_t, uint64_t>> SymAddrSizeMap;
  PLOProfile Profile;

  // Lock to access / modify global data structure.
  mutex Lock;

  list<unique_ptr<ELFView>> Views;
  // ELFCfgs are owned by ELFViews. Do not assume ownership here.
  // Same named Cfgs may exist in different object files (e.g. weak symbols.)
  struct ELFViewOrdinalComparator {
    bool operator()(const ELFView *A, const ELFView *B) {
      return A->Ordinal < B->Ordinal;
    }
  };
  map<StringRef, set<ELFView *, ELFViewOrdinalComparator>> CfgMap;

  // statistics
  atomic<uint32_t> TotalBB{0};
  atomic<uint32_t> TotalBBWoutAddr{0};
  atomic<uint32_t> ValidCfgs{0};
  atomic<uint32_t> InvalidCfgs{0};
  
};

extern PLO Plo;

template <class C>
void FreeContainer(C &container) {
  container.clear();
  C tmp;
  container.swap(tmp);
}

}  // namespace plo
}  // namespace lld


#endif
