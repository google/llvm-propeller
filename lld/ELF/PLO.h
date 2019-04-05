#ifndef LLD_ELF_PLO_H
#define LLD_ELF_PLO_H

#include <atomic>
#include <list>
#include <map>
#include <mutex>
#include <set>
#include <utility>
#include <vector>

#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/MemoryBuffer.h"

// This is toplevel PLO head file, do not include any other PLO*.h.

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

class ELFCfg;
class ELFCfgNode;
class ELFView;
class PLOProfile;

class PLO {
public:
  PLO();
  ~PLO();
  
  bool Init(StringRef &Symfile, StringRef &Profile);
  void ProcessFiles(vector<elf::InputFile *> &Files);

  bool FindCfgForAddress(uint64_t Addr,
			 ELFCfg *&ResultCfg,
			 ELFCfgNode *&ResultNode);

  // Addr -> Symbol (for all 't', 'T', 'w' and 'W' symbols) map.
  map<uint64_t, llvm::SmallVector<StringRef, 3>> AddrSymMap;
  // Sym -> <Addr, Size> map.
  map<StringRef, pair<uint64_t, uint64_t>> SymAddrSizeMap;

private:
  bool InitSymfile(StringRef &SymfileName);
  bool InitProfile(StringRef &ProfileName);

  void ProcessFile(const pair<elf::InputFile *, uint32_t> &Pair);
  bool SymContainsAddr(const StringRef &SymName,
		       uint64_t SymAddr,
		       uint64_t Addr,
		       StringRef &FuncName);

  // Parallizable, thread safety is guaranteed.
  void CreateCfgForFile(elf::InputFile *Inf);
  
  unique_ptr<PLOProfile> Profile;
  list<unique_ptr<ELFView>> Views;
  
  // Same named Cfgs may exist in different object files (e.g. weak symbols.)
  // We always choose symbols that appear earlier on the command line.
  struct ELFViewOrdinalComparator {
    bool Impl(const ELFView *A, const ELFView *B);
    bool operator()(const ELFView *A, const ELFView *B) {
      return Impl(A, B);
    }
  };
  map<StringRef, set<ELFView *, ELFViewOrdinalComparator>> CfgMap;

  
  // Lock to access / modify global data structure.
  mutex Lock;

public:
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
