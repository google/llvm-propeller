#include <stdio.h>
#include <fstream>
#include <string>

#include "PLOELFView.h"
#include "llvm/Support/MemoryBuffer.h"

using llvm::plo::ELFSizeInfo;
using llvm::plo::ELFView;

using llvm::MemoryBuffer;
using llvm::MemoryBufferRef;

using std::ifstream;
using std::string;

bool getSizeInfo(const string &Path, ELFSizeInfo *SizeInfo) {
  auto FileOrErr = MemoryBuffer::getFileOrSTDIN(Path);
  if (!FileOrErr) {
    fprintf(stderr, "Failed to open: \"%s\".\n", Path.c_str());
    return false;
  }

  MemoryBufferRef MBR(*(*FileOrErr));
  ELFView *EV = ELFView::Create(MBR);
  if (EV && EV->Init()) {
    EV->GetELFSizeInfo(SizeInfo);
    return true;
  }
  fprintf(stderr, "Failed to open: \"%s\" properly.\n", Path.c_str());
  return false;
}

int main(int argc, const char *argv[]) {
  if (argc <= 1)
    return 0;
  uint32_t errcnt = 0;
  uint32_t total = 0;
  ELFSizeInfo SizeInfo;
  ELFSizeInfo TotalSize;
  for (int i = 1; i < argc; ++i) {
    if (argv[i][0] == '@') {
        ifstream fin(argv[i] +1);
        if (!fin.good()) {
          fprintf(stderr, "Failed to process '%s'.\n", argv[i] + 1);
          return 1;
        }
        string line;
        while (std::getline(fin, line).good()) {
          if (line.empty() || line[0] == '#' || line[0] == ' ') {
            continue;
          }
          errcnt += getSizeInfo(line, &SizeInfo) ? 0 : 1;
          // SizeInfo is zeroed in case of error.
          TotalSize += SizeInfo;
          ++total;
        }
    } else {
        errcnt += getSizeInfo(argv[i], &SizeInfo) ? 0 : 1;
        TotalSize += SizeInfo;
        ++total;
    }
  }
  printf("Text: %lu\n", TotalSize.TextSize);
  printf("Alloc: %lu\n", TotalSize.OtherAllocSize);
  printf("SymTab: %lu\n", TotalSize.SymTabSize);
  printf("StrTab: %lu\n", TotalSize.StrTabSize);
  printf("FileSize: %lu\n", TotalSize.FileSize);
  printf("Files: %u/%u\n", errcnt, total);
  return 0;
}
