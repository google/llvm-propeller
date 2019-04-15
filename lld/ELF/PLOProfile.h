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

class PLO;

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

  PLO &Plo;
  StringRef ProfileName;

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
