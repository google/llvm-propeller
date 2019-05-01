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
#include "llvm/Support/Allocator.h"
#include "llvm/Support/StringSaver.h"

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
class CallGraph;

class PLO {
public:
  PLO();
  ~PLO();
  
  bool ProcessFiles(vector<elf::InputFile *> &Files,
                    StringRef &SymFileName,
                    StringRef &ProfileName,
                    StringRef &CfgDump);

  vector<StringRef> GenSymbolOrderingFile();

  // Addr -> Symbol (for all 't', 'T', 'w' and 'W' symbols) map.
  map<uint64_t, llvm::SmallVector<StringRef, 3>> AddrSymMap;
  // Sym -> <Addr, Size> map.
  map<StringRef, pair<uint64_t, uint64_t>> SymAddrSizeMap;
  llvm::BumpPtrAllocator BPAllocator;
  // StringRefs for AddrSymMap & SymAddrSizeMap. SymStringSaver is
  // huge and lives as long as PLO, so do not use lld arena, which
  // lasts till end of lld.
  llvm::StringSaver SymStrSaver;

public:
  template <class Visitor>
  void ForEachCfgRef(Visitor V) {
    for (auto &P : CfgMap) {
      V(*(*(P.second.begin())));
    }
  }

  static StringRef BBSymbol(StringRef &N) {
    StringRef::iterator P = (N.end() - 1), A = N.begin();
    char C = '\0';
    for ( ; P > A; --P) {
      C = *P;
      if (C == '.') break;
      if (C < '0' || C > '9') return StringRef("");
    }
    if (C == '.' && P - A >= 4 /* must be like "xx.bb." */ &&
        *(P - 1) == 'b' && *(P - 2) == 'b' && *(P - 3) == '.') {
      return StringRef(N.data(), P - A - 3);
    }
    return StringRef("");
  }

  template <class C>
  void FreeContainer(C &container) {
    container.clear();
    C tmp;
    container.swap(tmp);
  }


private:
  bool ProcessSymfile(StringRef &SymfileName);

  void ProcessFile(const pair<elf::InputFile *, uint32_t> &Pair);

  bool DumpCfgsToFile(StringRef &CfgDumpFile) const;
  void CalculateNodeFreqs();

  list<unique_ptr<ELFView>> Views;

  // Same named Cfgs may exist in different object files (e.g. weak
  // symbols.)  We always choose symbols that appear earlier on the
  // command line.  Note: implementation is in the .cpp file, because
  // ELFCfg here is an incomplete type.
  struct ELFViewOrdinalComparator {
    bool operator()(const ELFCfg *A, const ELFCfg *B);
  };
  map<StringRef, set<ELFCfg *, ELFViewOrdinalComparator>> CfgMap;

  // Lock to access / modify global data structure.
  mutex Lock;

  friend class PLOProfile;
  friend class CallGraph;
};

}  // namespace plo
}  // namespace lld


#endif
