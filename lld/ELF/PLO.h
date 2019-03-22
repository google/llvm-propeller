#ifndef LLD_ELF_PLO_H
#define LLD_ELF_PLO_H

#include <atomic>
#include <list>
#include <map>
#include <vector>

#include "PLOELFView.h"
#include "llvm/Support/MemoryBuffer.h"

using std::atomic;
using std::list;
using std::map;
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

  // Thread safety is guaranteed.
  void CreateCfgForFile(elf::InputFile *Inf);
  // Disposed of after CreateCfgForFile is done.
  map<StringRef, uint64_t> SymAddrMap;
  PLOProfile Profile;

  //
  list<unique_ptr<ELFView>> Views;
  // ELFCfgs are owned by ELFViews. Do not assume ownership here.
  map<uint64_t, ELFCfg *> GlobalCfgs;  // sorted by Cfg entry address.

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
