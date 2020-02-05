#ifndef LLVM_PROFILEDATA_PROPELLERPROF_H
#define LLVM_PROFILEDATA_PROPELLERPROF_H

#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"

using llvm::SmallSet;
using llvm::StringMap;
using llvm::StringRef;

namespace llvm {
namespace propeller {
bool getBBSectionsList(StringRef profFileName,
                       StringMap<SmallSet<unsigned, 4>> &bbMap);

}
} // namespace llvm

#endif
