#ifndef LLD_ELF_PLO_PROFILE_H
#define LLD_ELF_PLO_PROFILE_H

#include <list>
#include <map>
#include <ostream>
#include <utility>

#include <llvm/ADT/StringRef.h>

using std::list;
using std::map;
using std::ostream;
using std::pair;

using llvm::StringRef;

namespace lld {
namespace plo {

class PLO;
class ELFCfg;
class ELFCfgNode;

class LBREntry {
public:
  uint64_t From;
  uint64_t To;
  int      Cycles;
  char     Predict;

  static bool FillEntry(const StringRef &SR, LBREntry &Entry);
};

class PLOProfile {
public:
  PLOProfile(PLO &P) :Plo(P) {}
  ~PLOProfile();
  bool ProcessProfile(StringRef &ProfileName);

private:
  void ProcessLBR(LBREntry *EntryArray, int EntryIndex);
  
  bool FindCfgForAddress(uint64_t Addr,
                         ELFCfg *&ResultCfg,
                         ELFCfgNode *&ResultNode);
  
  bool SymContainsAddr(const StringRef &SymName,
                       uint64_t SymAddr,
                       uint64_t Addr,
                       StringRef &FuncName);

  PLO       &Plo;

  // Simple LRU cache to speed up lookup. Usually there are hundreds
  // of millions address lookup. This reduce total time from 7m to
  // 1m10s.
  inline void CacheSearchResult(uint64_t Addr, ELFCfgNode *Node);
  // Time -> Address mapping.
  uint64_t SearchTime{0};
  // Use 1M cache.
  uint32_t MaxCachedResults =
    16 * 1024 * 1024 / sizeof(map<uint64_t, ELFCfgNode *>::value_type);
  // Access time -> address map.
  map<uint64_t, uint64_t>     SearchTimeMap;
  // Address -> <access time, Node *> map.
  map<uint64_t, pair<uint64_t, ELFCfgNode *>> SearchCacheMap;

  // Statistics.
  uint64_t  IntraFunc{0};
  uint64_t  NonMarkedIntraFunc{0};
  uint64_t  InterFunc{0};
  uint64_t  NonMarkedInterFunc{0};
};

ostream & operator << (ostream &Out, const LBREntry &Entry);

}
}

#endif
