#ifndef LLD_ELF_PLO_H
#define LLD_ELF_PLO_H

#include "llvm/Support/MemoryBuffer.h"

using llvm::MemoryBufferRef;

namespace lld {

namespace elf {
  class InputFile;
}

namespace plo {

  // Thread safety is guaranteed.
  void ProcessFile(elf::InputFile *Inf);
  

}  // namespace plo
}  // namespace lld


#endif
