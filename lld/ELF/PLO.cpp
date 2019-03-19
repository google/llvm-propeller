#include "PLO.h"

#include "InputFiles.h"
#include "PLOELFCfg.h"
#include "PLOELFView.h"

namespace lld {
namespace plo {

  void ProcessFile(lld::elf::InputFile *Inf) {
    if (Inf->getName() == "test.o") {
      fprintf(stderr, "(shenhan): processing %s\n", Inf->getName().str().c_str());
      ELFView *View = ELFView::Create(Inf->MB);
      if (View && View->Init()) {
	View->BuildCfgs();
      }
    }
  }

}  // namespace plo
}  // namespace lld
