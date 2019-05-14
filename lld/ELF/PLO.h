#ifndef LLD_ELF_PLO_H
#define LLD_ELF_PLO_H

#include <atomic>
#include <list>
#include <map>
#include <mutex>
#include <set>
#include <tuple>
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
using std::tuple;
using std::unique_ptr;
using std::vector;

using llvm::MemoryBufferRef;
using llvm::StringRef;

namespace lld {
namespace elf {
  class InputFile;
  class SymbolTable;
}

namespace plo {

class ELFCfg;
class ELFCfgNode;
class ELFView;
class PLOProfile;
class CallGraph;

class Symfile {
public:
  Symfile();
  ~Symfile();

  // <Name, Addr, Size>
  using Sym = tuple<StringRef, uint32_t, uint32_t>;
  using SymHandler = list<Sym>::iterator;

  StringRef getName(SymHandler S) { return std::get<0>(*S); }
  uint32_t  getAddr(SymHandler S) { return std::get<1>(*S); }
  uint32_t  getSize(SymHandler S) { return std::get<2>(*S); }
  
  map<StringRef, SymHandler> NameMap;
  map<uint64_t, llvm::SmallVector<SymHandler, 3>> AddrMap;

  bool init(StringRef SymfileName);
  void reset() {
    map<StringRef, SymHandler> T0(std::move(NameMap));
    map<uint64_t, llvm::SmallVector<SymHandler, 3>> T1(std::move(AddrMap));
    SymList.clear();
    BPAllocator.Reset();
  }

private:
  list<Sym> SymList;

  llvm::BumpPtrAllocator BPAllocator;
  // StringRefs for symbol names. SymStringSaver is huge and lives
  // only as long as Symfile, so do not use lld arena, which lasts
  // till end of lld.
  llvm::StringSaver SymStrSaver;
};

class PLO {
public:
  PLO(lld::elf::SymbolTable *ST);
  ~PLO();
  
  bool processFiles(vector<elf::InputFile *> &Files,
                    StringRef &SymFileName,
                    StringRef &ProfileName,
                    StringRef &CfgDump);

  vector<StringRef> genSymbolOrderingFile();

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

  Symfile Syms;
  lld::elf::SymbolTable *Symtab;

private:
  void processFile(const pair<elf::InputFile *, uint32_t> &Pair);

  bool dumpCfgsToFile(StringRef &CfgDumpFile) const;
  void calculateNodeFreqs();

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
