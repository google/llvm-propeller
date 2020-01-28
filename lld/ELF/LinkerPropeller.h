//===- LinkerPropeller.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is the interface between LLD/ELF and Propeller. All interactions
// between LLD/ELF and propeller must be defined here.
//
// All dependencies on lld/ELF/*.h must happen in this file and
// LinkerPropeller.cpp.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_ELF_LINKER_PROPELLER_H
#define LLD_ELF_LINKER_PROPELLER_H

#include "llvm/ADT/StringRef.h"

namespace lld {
namespace propeller {
// Propeller interface to lld.
void doPropeller();

// Returns true if this is a BB symbol and shall be kept in the final binary's
// strtab.
bool isBBSymbolAndKeepIt(llvm::StringRef N);
} // namespace propeller
} // namespace lld

#endif
