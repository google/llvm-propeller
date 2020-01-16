//===- LinkerPropeller.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is the implementation of interface between LLD/ELF and Propeller.
//
// Current implementation first copies propeller parameters from
// lld::elf::Config instances into PropellerConfig. Then it transforms the
// vector<lld::elf::InputFile> into vector<lld::propeller::ObjectView>.
//
// "doPropeller" then passes PropellerConfig and vector<ObjectView> to Propeller
// instance, and finally, after Propeller is done with its work, doPropeller
// passes the resulting symboll ordering back to lld.
//
// In summary, the dependencies of Propeller are:
//   - a set of "lld::elf::InputFile"s.
//   - command line arguments in lld::elf::Config
//   - lld's being able to arrange section orders according to a vector of
//     symbol names.
//
// All lld/ELF/Propeller/* only uses headers from lld/include, and llvm/include.
//
//===----------------------------------------------------------------------===//

#include "LinkerPropeller.h"

#include "Config.h"
#include "InputFiles.h"
#include "Propeller/Propeller.h"
#include "Propeller/PropellerCFG.h"
#include "Propeller/PropellerConfig.h"
#include "lld/Common/Memory.h"

#include "lld/Common/ErrorHandler.h"

#include <algorithm>
#include <vector>

namespace lld {

using elf::config;
using elf::InputFile;
using elf::objectFiles;

namespace propeller {

Propeller *prop;

// Set up PropellerConfig from global lld config instnace.
static void setupConfig() {
  propellerConfig.optPropeller = config->propeller;
  propellerConfig.optLinkerOutputFile = config->outputFile;

#define COPY_CONFIG(NAME) propellerConfig.opt##NAME = config->propeller##NAME
  COPY_CONFIG(BackwardJumpDistance);
  COPY_CONFIG(BackwardJumpWeight);
  COPY_CONFIG(BBOrder);
  COPY_CONFIG(ChainSplitThreshold);
  COPY_CONFIG(DebugSymbols);
  COPY_CONFIG(DumpCfgs);
  COPY_CONFIG(DumpSymbolOrder);
  COPY_CONFIG(FallthroughWeight);
  COPY_CONFIG(ForwardJumpDistance);
  COPY_CONFIG(ForwardJumpWeight);
  COPY_CONFIG(Opts);
  COPY_CONFIG(PrintStats);
  COPY_CONFIG(ReorderBlocks);
  COPY_CONFIG(ReorderFuncs);
  COPY_CONFIG(SplitFuncs);
  COPY_CONFIG(ReorderIP);
#undef COPY_CONFIG
}

// Propeller framework entrance.
void doPropeller() {
  if (config->propeller.empty())
    return;

  setupConfig();
  prop = make<Propeller>();

  if (!prop->checkTarget()) {
    warn("[Propeller]: Propeller skipped '" + config->outputFile + "'.");
    return;
  }

  std::vector<ObjectView *> objectViews;
  std::for_each(objectFiles.begin(), objectFiles.end(),
                [&objectViews](const InputFile *inf) {
                  auto *ov = Propeller::createObjectView(
                      inf->getName(), objectViews.size() + 1, inf->mb);
                  if (ov)
                    objectViews.push_back(ov);
                });
  if (prop->processFiles(objectViews))
    config->symbolOrderingFile = prop->genSymbolOrderingFile();
  else
    error("Propeller stage failed.");
}

} // namespace propeller
} // namespace lld
