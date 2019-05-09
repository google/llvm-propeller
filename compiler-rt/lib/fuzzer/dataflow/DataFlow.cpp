/*===- DataFlow.cpp - a standalone DataFlow tracer                  -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// An experimental data-flow tracer for fuzz targets.
// It is based on DFSan and SanitizerCoverage.
// https://clang.llvm.org/docs/DataFlowSanitizer.html
// https://clang.llvm.org/docs/SanitizerCoverage.html#tracing-data-flow
//
// It executes the fuzz target on the given input while monitoring the
// data flow for every instrumented comparison instruction.
//
// The output shows which functions depend on which bytes of the input,
// and also provides basic-block coverage for every input.
//
// Build:
//   1. Compile this file with -fsanitize=dataflow
//   2. Build the fuzz target with -g -fsanitize=dataflow
//       -fsanitize-coverage=trace-pc-guard,pc-table,bb,trace-cmp
//   3. Link those together with -fsanitize=dataflow
//
//  -fsanitize-coverage=trace-cmp inserts callbacks around every comparison
//  instruction, DFSan modifies the calls to pass the data flow labels.
//  The callbacks update the data flow label for the current function.
//  See e.g. __dfsw___sanitizer_cov_trace_cmp1 below.
//
//  -fsanitize-coverage=trace-pc-guard,pc-table,bb instruments function
//  entries so that the comparison callback knows that current function.
//  -fsanitize-coverage=...,bb also allows to collect basic block coverage.
//
//
// Run:
//   # Collect data flow and coverage for INPUT_FILE
//   # write to OUTPUT_FILE (default: stdout)
//   ./a.out FIRST_LABEL LAST_LABEL INPUT_FILE [OUTPUT_FILE]
//
//   # Print all instrumented functions. llvm-symbolizer must be present in PATH
//   ./a.out
//
// Example output:
// ===============
//  F0 11111111111111
//  F1 10000000000000
//  C0 1 2 3 4 5
//  C1 8
//  ===============
// "FN xxxxxxxxxx": tells what bytes of the input does the function N depend on.
//    The byte string is LEN+1 bytes. The last byte is set if the function
//    depends on the input length.
// "CN X Y Z T": tells that a function N has basic blocks X, Y, and Z covered
//    in addition to the function's entry block, out of T total instrumented
//    blocks.
//
//===----------------------------------------------------------------------===*/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <execinfo.h>  // backtrace_symbols_fd

#include <sanitizer/dfsan_interface.h>

extern "C" {
extern int LLVMFuzzerTestOneInput(const unsigned char *Data, size_t Size);
__attribute__((weak)) extern int LLVMFuzzerInitialize(int *argc, char ***argv);
} // extern "C"

static size_t InputLen;
static size_t InputLabelBeg;
static size_t InputLabelEnd;
static size_t InputSizeLabel;
static size_t NumFuncs, NumGuards;
static uint32_t *GuardsBeg, *GuardsEnd;
static const uintptr_t *PCsBeg, *PCsEnd;
static __thread size_t CurrentFunc;
static dfsan_label *FuncLabels;  // Array of NumFuncs elements.
static bool *BBExecuted;  // Array of NumGuards elements.
static char *PrintableStringForLabel;  // InputLen + 2 bytes.
static bool LabelSeen[1 << 8 * sizeof(dfsan_label)];

enum {
  PCFLAG_FUNC_ENTRY = 1,
};

static inline bool BlockIsEntry(size_t BlockIdx) {
  return PCsBeg[BlockIdx * 2 + 1] & PCFLAG_FUNC_ENTRY;
}

// Prints all instrumented functions.
static int PrintFunctions() {
  // We don't have the symbolizer integrated with dfsan yet.
  // So use backtrace_symbols_fd and pipe it through llvm-symbolizer.
  // TODO(kcc): this is pretty ugly and may break in lots of ways.
  //      We'll need to make a proper in-process symbolizer work with DFSan.
  FILE *Pipe = popen("sed 's/(+/ /g; s/).*//g' "
                     "| llvm-symbolizer "
                     "| grep 'dfs\\$' "
                     "| sed 's/dfs\\$//g'", "w");
  for (size_t I = 0; I < NumGuards; I++) {
    uintptr_t PC = PCsBeg[I * 2];
    if (!BlockIsEntry(I)) continue;
    void *const Buf[1] = {(void*)PC};
    backtrace_symbols_fd(Buf, 1, fileno(Pipe));
  }
  pclose(Pipe);
  return 0;
}

extern "C"
void SetBytesForLabel(dfsan_label L, char *Bytes) {
  if (LabelSeen[L])
    return;
  LabelSeen[L] = true;
  assert(L);
  if (L < InputSizeLabel) {
    Bytes[L + InputLabelBeg - 1] = '1';
  } else if (L == InputSizeLabel) {
    Bytes[InputLen] = '1';
  } else {
    auto *DLI = dfsan_get_label_info(L);
    SetBytesForLabel(DLI->l1, Bytes);
    SetBytesForLabel(DLI->l2, Bytes);
  }
}

static char *GetPrintableStringForLabel(dfsan_label L) {
  memset(PrintableStringForLabel, '0', InputLen + 1);
  PrintableStringForLabel[InputLen + 1] = 0;
  memset(LabelSeen, 0, sizeof(LabelSeen));
  SetBytesForLabel(L, PrintableStringForLabel);
  return PrintableStringForLabel;
}

static void PrintDataFlow(FILE *Out) {
  for (size_t I = 0; I < NumFuncs; I++)
    if (FuncLabels[I])
      fprintf(Out, "F%zd %s\n", I, GetPrintableStringForLabel(FuncLabels[I]));
}

static void PrintCoverage(FILE *Out) {
  ssize_t CurrentFuncGuard = -1;
  ssize_t CurrentFuncNum = -1;
  ssize_t NumBlocksInCurrentFunc = -1;
  for (size_t FuncBeg = 0; FuncBeg < NumGuards;) {
    CurrentFuncNum++;
    assert(BlockIsEntry(FuncBeg));
    size_t FuncEnd = FuncBeg + 1;
    for (; FuncEnd < NumGuards && !BlockIsEntry(FuncEnd); FuncEnd++)
      ;
    if (BBExecuted[FuncBeg]) {
      fprintf(Out, "C%zd", CurrentFuncNum);
      for (size_t I = FuncBeg + 1; I < FuncEnd; I++)
        if (BBExecuted[I])
          fprintf(Out, " %zd", I - FuncBeg);
      fprintf(Out, " %zd\n", FuncEnd - FuncBeg);
    }
    FuncBeg = FuncEnd;
  }
}

int main(int argc, char **argv) {
  if (LLVMFuzzerInitialize)
    LLVMFuzzerInitialize(&argc, &argv);
  if (argc == 1)
    return PrintFunctions();
  assert(argc == 4 || argc == 5);
  InputLabelBeg = atoi(argv[1]);
  InputLabelEnd = atoi(argv[2]);
  assert(InputLabelBeg < InputLabelEnd);

  const char *Input = argv[3];
  fprintf(stderr, "INFO: reading '%s'\n", Input);
  FILE *In = fopen(Input, "r");
  assert(In);
  fseek(In, 0, SEEK_END);
  InputLen = ftell(In);
  fseek(In, 0, SEEK_SET);
  unsigned char *Buf = (unsigned char*)malloc(InputLen);
  size_t NumBytesRead = fread(Buf, 1, InputLen, In);
  assert(NumBytesRead == InputLen);
  PrintableStringForLabel = (char*)malloc(InputLen + 2);
  fclose(In);

  fprintf(stderr, "INFO: running '%s'\n", Input);
  for (size_t I = 1; I <= InputLen; I++) {
    size_t Idx = I - 1;
    if (Idx >= InputLabelBeg && Idx < InputLabelEnd) {
      dfsan_label L = dfsan_create_label("", nullptr);
      assert(L == I - InputLabelBeg);
      dfsan_set_label(L, Buf + Idx, 1);
    }
  }
  dfsan_label SizeL = dfsan_create_label("", nullptr);
  InputSizeLabel = SizeL;
  assert(InputSizeLabel == InputLabelEnd - InputLabelBeg + 1);
  dfsan_set_label(SizeL, &InputLen, sizeof(InputLen));

  LLVMFuzzerTestOneInput(Buf, InputLen);
  free(Buf);

  bool OutIsStdout = argc == 4;
  fprintf(stderr, "INFO: writing dataflow to %s\n",
          OutIsStdout ? "<stdout>" : argv[4]);
  FILE *Out = OutIsStdout ? stdout : fopen(argv[4], "w");
  PrintDataFlow(Out);
  PrintCoverage(Out);
  if (!OutIsStdout) fclose(Out);
}

extern "C" {

void __sanitizer_cov_trace_pc_guard_init(uint32_t *start,
                                         uint32_t *stop) {
  assert(NumFuncs == 0 && "This tool does not support DSOs");
  assert(start < stop && "The code is not instrumented for coverage");
  if (start == stop || *start) return;  // Initialize only once.
  GuardsBeg = start;
  GuardsEnd = stop;
}

void __sanitizer_cov_pcs_init(const uintptr_t *pcs_beg,
                              const uintptr_t *pcs_end) {
  if (NumGuards) return;  // Initialize only once.
  NumGuards = GuardsEnd - GuardsBeg;
  PCsBeg = pcs_beg;
  PCsEnd = pcs_end;
  assert(NumGuards == (PCsEnd - PCsBeg) / 2);
  for (size_t i = 0; i < NumGuards; i++) {
    if (BlockIsEntry(i)) {
      NumFuncs++;
      GuardsBeg[i] = NumFuncs;
    }
  }
  FuncLabels = (dfsan_label*)calloc(NumFuncs, sizeof(dfsan_label));
  BBExecuted = (bool*)calloc(NumGuards, sizeof(bool));
  fprintf(stderr, "INFO: %zd instrumented function(s) observed "
          "and %zd basic blocks\n", NumFuncs, NumGuards);
}

void __sanitizer_cov_trace_pc_indir(uint64_t x){}  // unused.

void __sanitizer_cov_trace_pc_guard(uint32_t *guard) {
  size_t GuardIdx = guard - GuardsBeg;
  assert(GuardIdx < NumGuards);
  BBExecuted[GuardIdx] = true;
  if (!*guard) return;  // not a function entry.
  uint32_t FuncNum = *guard - 1;  // Guards start from 1.
  assert(FuncNum < NumFuncs);
  CurrentFunc = FuncNum;
}

void __dfsw___sanitizer_cov_trace_switch(uint64_t Val, uint64_t *Cases,
                                         dfsan_label L1, dfsan_label UnusedL) {
  assert(CurrentFunc < NumFuncs);
  FuncLabels[CurrentFunc] = dfsan_union(FuncLabels[CurrentFunc], L1);
}

#define HOOK(Name, Type)                                                       \
  void Name(Type Arg1, Type Arg2, dfsan_label L1, dfsan_label L2) {            \
    assert(CurrentFunc < NumFuncs);                                            \
    FuncLabels[CurrentFunc] =                                                  \
        dfsan_union(FuncLabels[CurrentFunc], dfsan_union(L1, L2));             \
  }

HOOK(__dfsw___sanitizer_cov_trace_const_cmp1, uint8_t)
HOOK(__dfsw___sanitizer_cov_trace_const_cmp2, uint16_t)
HOOK(__dfsw___sanitizer_cov_trace_const_cmp4, uint32_t)
HOOK(__dfsw___sanitizer_cov_trace_const_cmp8, uint64_t)
HOOK(__dfsw___sanitizer_cov_trace_cmp1, uint8_t)
HOOK(__dfsw___sanitizer_cov_trace_cmp2, uint16_t)
HOOK(__dfsw___sanitizer_cov_trace_cmp4, uint32_t)
HOOK(__dfsw___sanitizer_cov_trace_cmp8, uint64_t)

} // extern "C"
