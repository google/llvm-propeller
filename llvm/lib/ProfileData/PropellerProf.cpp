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

bool getBBSectionsList(StringRef profFileName,
                       StringMap<SmallSet<unsigned, 4>>& bbMap) {
  if (profFileName.empty())
    return false;

  std::ifstream fin(profFileName);
  if (!fin.good()) {
    // errs() << "Cannot open " + config->ltoBBSections;
    return false;
  }
  
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
      else {
        // errs() << "Found \"!!\" without preceding \"!\"";
        return false;
      }
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