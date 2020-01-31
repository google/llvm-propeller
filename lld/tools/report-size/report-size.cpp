#include <fstream>
#include <stdio.h>
#include <string>
#include <vector>

#include "PLOELFView.h"
#include "llvm/Support/MemoryBuffer.h"

using llvm::plo::ELFSizeInfo;
using llvm::plo::ELFView;

using llvm::MemoryBuffer;
using llvm::MemoryBufferRef;

using std::ifstream;
using std::string;
using std::vector;

bool getSizeInfo(const string &Path, ELFSizeInfo *SizeInfo) {
  auto FileOrErr = MemoryBuffer::getFileOrSTDIN(Path);
  if (!FileOrErr) {
    fprintf(stderr, "Failed to open: \"%s\".\n", Path.c_str());
    return false;
  }

  MemoryBufferRef MBR(*(*FileOrErr));
  ELFView *EV = ELFView::Create(MBR);
  if (!EV) {
    fprintf(stderr, "Failed to create ELF instance for \"%s\"\n", Path.c_str());
    return false;
  }
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
      ifstream fin(argv[i] + 1);
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

  auto CommaPrint = [](uint64_t Num) {
    const int right_aligned_to_column = 25;
    if (Num == 0) {
      printf("%s0", std::string(right_aligned_to_column - 1, ' ').c_str());
      return;
    }
    vector<uint64_t> Nums;
    uint64_t D = Num;
    while (D) {
      Nums.push_back(D % 1000);
      D /= 1000;
    }
    uint64_t FirstNum = *(Nums.rbegin());
    int space_needed = right_aligned_to_column - ((Nums.size() - 1) << 2) -
                       (FirstNum >= 100 ? 1 : 0) - (FirstNum >= 10 ? 1 : 0) - 1;
    printf("%s", std::string(space_needed, ' ').c_str());
    for (auto O = Nums.rbegin(), P = Nums.rbegin(), Q = Nums.rend(); P != Q;
         ++P) {
      if (P != O) {
        printf(",%03lu", *P);
      } else {
        printf("%lu", *P);
      }
    }
  };
  auto PrintResult = [&CommaPrint](const char *Prefix, uint64_t Num) {
    printf("%s ", Prefix);
    CommaPrint(Num);
    printf("\n");
  };
  PrintResult("Text:       ", TotalSize.TextSize);
  PrintResult("Alloc:      ", TotalSize.OtherAllocSize);
  PrintResult("Rela:       ", TotalSize.RelaSize);
  PrintResult("EHFrames:   ", TotalSize.EhFrameRelatedSize);
  PrintResult("SymTab:     ", TotalSize.SymTabSize);
  PrintResult("SymEntries: ", TotalSize.SymTabEntryNum);
  PrintResult("StrTab:     ", TotalSize.StrTabSize);
  PrintResult("FileSize:   ", TotalSize.FileSize);
  { printf("Files (err/total): %u/%u\n", errcnt, total); }
  return 0;
}
