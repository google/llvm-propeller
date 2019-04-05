#ifndef LLD_ELF_PLO_PROFILE_H
#define LLD_ELF_PLO_PROFILE_H

#include <list>
#include <memory>
#include <ostream>

#include <llvm/ADT/StringRef.h>

using std::list;
using std::ostream;
using std::unique_ptr;

using llvm::StringRef;

namespace lld {
namespace plo {

class LBREntry {
public:
  uint64_t From;
  uint64_t To;
  int      Cycles;
  char     Predict;

  static LBREntry *CreateEntry(const StringRef &SR);
};

class LBRRecord {
public:
  list<unique_ptr<LBREntry>> Entries;
};

class PLOProfile {
public:
  list<unique_ptr<LBRRecord>> LBRs;

  void ProcessLBRs();
};

ostream & operator << (ostream &Out, const LBREntry &Entry);
ostream & operator << (ostream &Out, const LBRRecord &R);

}
}

#endif
