#include "llvm/ProfileData/PropellerProf.h"

#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"

#include <fstream>
#include <string>

using llvm::SmallSet;
using llvm::StringMap;
using llvm::StringRef;

namespace llvm {
namespace propeller {

// Basic Block Sections can be enabled for a subset of machine basic blocks.
// This is done by passing a file containing names of functions for which basic
// block sections are desired.  Additionally, machine basic block ids of the
// functions can also be specified for a finer granularity.
// A file with basic block sections for all of function main and two blocks for
// function foo looks like this:
// ----------------------------
// list.txt:
// !main
// !foo
// !!2
// !!4
bool getBBSectionsList(StringRef profFileName,
                       StringMap<SmallSet<unsigned, 4>> &bbMap) {
  if (profFileName.empty())
    return false;

  std::ifstream fin(profFileName);
  if (!fin.good())
    return false;

  StringMap<SmallSet<unsigned, 4>>::iterator fi = bbMap.end();
  std::string line;
  while ((std::getline(fin, line)).good()) {
    StringRef S(line);
    // Lines beginning with @, # are not useful here.
    if (S.empty() || S[0] == '@' || S[0] == '#')
      continue;
    if (!S.consume_front("!") || S.empty())
      break;
    if (S.consume_front("!")) {
      if (fi != bbMap.end())
        fi->second.insert(std::stoi(S));
      else
        return false;
    } else {
      // Start a new function.
      auto R = bbMap.try_emplace(S.split('/').first);
      fi = R.first;
      assert(R.second);
    }
  }
  return true;
}

} // namespace propeller
} // namespace llvm
