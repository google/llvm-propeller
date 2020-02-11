#include "llvm/ProfileData/BBSectionsProf.h"

#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/LineIterator.h"
#include "llvm/Support/MemoryBuffer.h"

#include <string>

using llvm::SmallSet;
using llvm::StringMap;
using llvm::StringRef;

namespace llvm {
namespace bbsections {

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

  auto MbOrErr = MemoryBuffer::getFile(profFileName);
  if (MbOrErr.getError())
    return false;

  MemoryBuffer &Buffer = *MbOrErr.get();
  line_iterator LineIt(Buffer, /*SkipBlanks=*/true, /*CommentMarker=*/'#');

  StringMap<SmallSet<unsigned, 4>>::iterator fi = bbMap.end();

  for (; !LineIt.is_at_eof(); ++LineIt) {
    StringRef s(*LineIt);
    if (s[0] == '@')
      continue;
    // Check for the leading "!"
    if (!s.consume_front("!") || s.empty())
      break;
    // Check for second "!" which encodes basic block ids.
    if (s.consume_front("!")) {
      if (fi != bbMap.end())
        fi->second.insert(std::stoi(s.str()));
      else
        return false;
    } else {
      // Start a new function.
      auto R = bbMap.try_emplace(s.split('/').first);
      fi = R.first;
      assert(R.second);
    }
  }
  return true;
}

} // namespace bbsections
} // namespace llvm
