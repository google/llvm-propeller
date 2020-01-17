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

namespace lld {
namespace propeller {
// Propeller interface to lld.
void doPropeller();
} // namespace propeller
} // namespace lld

#endif
