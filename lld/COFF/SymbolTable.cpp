//===- SymbolTable.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SymbolTable.h"
#include "Config.h"
#include "Driver.h"
#include "LTO.h"
#include "PDB.h"
#include "Symbols.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/Memory.h"
#include "lld/Common/Timer.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Object/WindowsMachineFlag.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <utility>

using namespace llvm;

namespace lld {
namespace coff {

static Timer LTOTimer("LTO", Timer::root());

SymbolTable *Symtab;

void SymbolTable::addFile(InputFile *File) {
  log("Reading " + toString(File));
  File->parse();

  MachineTypes MT = File->getMachineType();
  if (Config->Machine == IMAGE_FILE_MACHINE_UNKNOWN) {
    Config->Machine = MT;
  } else if (MT != IMAGE_FILE_MACHINE_UNKNOWN && Config->Machine != MT) {
    error(toString(File) + ": machine type " + machineToStr(MT) +
          " conflicts with " + machineToStr(Config->Machine));
    return;
  }

  if (auto *F = dyn_cast<ObjFile>(File)) {
    ObjFile::Instances.push_back(F);
  } else if (auto *F = dyn_cast<BitcodeFile>(File)) {
    BitcodeFile::Instances.push_back(F);
  } else if (auto *F = dyn_cast<ImportFile>(File)) {
    ImportFile::Instances.push_back(F);
  }

  Driver->parseDirectives(File);
}

static void errorOrWarn(const Twine &S) {
  if (Config->ForceUnresolved)
    warn(S);
  else
    error(S);
}

// Returns the symbol in SC whose value is <= Addr that is closest to Addr.
// This is generally the global variable or function whose definition contains
// Addr.
static Symbol *getSymbol(SectionChunk *SC, uint32_t Addr) {
  DefinedRegular *Candidate = nullptr;

  for (Symbol *S : SC->File->getSymbols()) {
    auto *D = dyn_cast_or_null<DefinedRegular>(S);
    if (!D || D->getChunk() != SC || D->getValue() > Addr ||
        (Candidate && D->getValue() < Candidate->getValue()))
      continue;

    Candidate = D;
  }

  return Candidate;
}

// Given a file and the index of a symbol in that file, returns a description
// of all references to that symbol from that file. If no debug information is
// available, returns just the name of the file, else one string per actual
// reference as described in the debug info.
std::vector<std::string> getSymbolLocations(ObjFile *File, uint32_t SymIndex) {
  struct Location {
    Symbol *Sym;
    std::pair<StringRef, uint32_t> FileLine;
  };
  std::vector<Location> Locations;

  for (Chunk *C : File->getChunks()) {
    auto *SC = dyn_cast<SectionChunk>(C);
    if (!SC)
      continue;
    for (const coff_relocation &R : SC->getRelocs()) {
      if (R.SymbolTableIndex != SymIndex)
        continue;
      std::pair<StringRef, uint32_t> FileLine =
          getFileLine(SC, R.VirtualAddress);
      Symbol *Sym = getSymbol(SC, R.VirtualAddress);
      if (!FileLine.first.empty() || Sym)
        Locations.push_back({Sym, FileLine});
    }
  }

  if (Locations.empty())
    return std::vector<std::string>({"\n>>> referenced by " + toString(File)});

  std::vector<std::string> SymbolLocations(Locations.size());
  size_t I = 0;
  for (Location Loc : Locations) {
    llvm::raw_string_ostream OS(SymbolLocations[I++]);
    OS << "\n>>> referenced by ";
    if (!Loc.FileLine.first.empty())
      OS << Loc.FileLine.first << ":" << Loc.FileLine.second
         << "\n>>>               ";
    OS << toString(File);
    if (Loc.Sym)
      OS << ":(" << toString(*Loc.Sym) << ')';
  }
  return SymbolLocations;
}

// For an undefined symbol, stores all files referencing it and the index of
// the undefined symbol in each file.
struct UndefinedDiag {
  Symbol *Sym;
  struct File {
    ObjFile *OFile;
    uint64_t SymIndex;
  };
  std::vector<File> Files;
};

static void reportUndefinedSymbol(const UndefinedDiag &UndefDiag) {
  std::string Out;
  llvm::raw_string_ostream OS(Out);
  OS << "undefined symbol: " << toString(*UndefDiag.Sym);

  const size_t MaxUndefReferences = 10;
  size_t I = 0, NumRefs = 0;
  for (const UndefinedDiag::File &Ref : UndefDiag.Files) {
    std::vector<std::string> SymbolLocations =
        getSymbolLocations(Ref.OFile, Ref.SymIndex);
    NumRefs += SymbolLocations.size();
    for (const std::string &S : SymbolLocations) {
      if (I >= MaxUndefReferences)
        break;
      OS << S;
      I++;
    }
  }
  if (I < NumRefs)
    OS << "\n>>> referenced " << NumRefs - I << " more times";
  errorOrWarn(OS.str());
}

void SymbolTable::loadMinGWAutomaticImports() {
  for (auto &I : SymMap) {
    Symbol *Sym = I.second;
    auto *Undef = dyn_cast<Undefined>(Sym);
    if (!Undef)
      continue;
    if (!Sym->IsUsedInRegularObj)
      continue;

    StringRef Name = Undef->getName();

    if (Name.startswith("__imp_"))
      continue;
    // If we have an undefined symbol, but we have a Lazy representing a
    // symbol we could load from file, make sure to load that.
    Lazy *L = dyn_cast_or_null<Lazy>(find(("__imp_" + Name).str()));
    if (!L || L->PendingArchiveLoad)
      continue;

    log("Loading lazy " + L->getName() + " from " + L->File->getName() +
        " for automatic import");
    L->PendingArchiveLoad = true;
    L->File->addMember(&L->Sym);
  }
}

bool SymbolTable::handleMinGWAutomaticImport(Symbol *Sym, StringRef Name) {
  if (Name.startswith("__imp_"))
    return false;
  Defined *Imp = dyn_cast_or_null<Defined>(find(("__imp_" + Name).str()));
  if (!Imp)
    return false;

  // Replace the reference directly to a variable with a reference
  // to the import address table instead. This obviously isn't right,
  // but we mark the symbol as IsRuntimePseudoReloc, and a later pass
  // will add runtime pseudo relocations for every relocation against
  // this Symbol. The runtime pseudo relocation framework expects the
  // reference itself to point at the IAT entry.
  size_t ImpSize = 0;
  if (isa<DefinedImportData>(Imp)) {
    log("Automatically importing " + Name + " from " +
        cast<DefinedImportData>(Imp)->getDLLName());
    ImpSize = sizeof(DefinedImportData);
  } else if (isa<DefinedRegular>(Imp)) {
    log("Automatically importing " + Name + " from " +
        toString(cast<DefinedRegular>(Imp)->File));
    ImpSize = sizeof(DefinedRegular);
  } else {
    warn("unable to automatically import " + Name + " from " + Imp->getName() +
         " from " + toString(cast<DefinedRegular>(Imp)->File) +
         "; unexpected symbol type");
    return false;
  }
  Sym->replaceKeepingName(Imp, ImpSize);
  Sym->IsRuntimePseudoReloc = true;

  // There may exist symbols named .refptr.<name> which only consist
  // of a single pointer to <name>. If it turns out <name> is
  // automatically imported, we don't need to keep the .refptr.<name>
  // pointer at all, but redirect all accesses to it to the IAT entry
  // for __imp_<name> instead, and drop the whole .refptr.<name> chunk.
  DefinedRegular *Refptr =
      dyn_cast_or_null<DefinedRegular>(find((".refptr." + Name).str()));
  if (Refptr && Refptr->getChunk()->getSize() == Config->Wordsize) {
    SectionChunk *SC = dyn_cast_or_null<SectionChunk>(Refptr->getChunk());
    if (SC && SC->getRelocs().size() == 1 && *SC->symbols().begin() == Sym) {
      log("Replacing .refptr." + Name + " with " + Imp->getName());
      Refptr->getChunk()->Live = false;
      Refptr->replaceKeepingName(Imp, ImpSize);
    }
  }
  return true;
}

void SymbolTable::reportRemainingUndefines() {
  SmallPtrSet<Symbol *, 8> Undefs;
  DenseMap<Symbol *, Symbol *> LocalImports;

  for (auto &I : SymMap) {
    Symbol *Sym = I.second;
    auto *Undef = dyn_cast<Undefined>(Sym);
    if (!Undef)
      continue;
    if (!Sym->IsUsedInRegularObj)
      continue;

    StringRef Name = Undef->getName();

    // A weak alias may have been resolved, so check for that.
    if (Defined *D = Undef->getWeakAlias()) {
      // We want to replace Sym with D. However, we can't just blindly
      // copy sizeof(SymbolUnion) bytes from D to Sym because D may be an
      // internal symbol, and internal symbols are stored as "unparented"
      // Symbols. For that reason we need to check which type of symbol we
      // are dealing with and copy the correct number of bytes.
      if (isa<DefinedRegular>(D))
        memcpy(Sym, D, sizeof(DefinedRegular));
      else if (isa<DefinedAbsolute>(D))
        memcpy(Sym, D, sizeof(DefinedAbsolute));
      else
        memcpy(Sym, D, sizeof(SymbolUnion));
      continue;
    }

    // If we can resolve a symbol by removing __imp_ prefix, do that.
    // This odd rule is for compatibility with MSVC linker.
    if (Name.startswith("__imp_")) {
      Symbol *Imp = find(Name.substr(strlen("__imp_")));
      if (Imp && isa<Defined>(Imp)) {
        auto *D = cast<Defined>(Imp);
        replaceSymbol<DefinedLocalImport>(Sym, Name, D);
        LocalImportChunks.push_back(cast<DefinedLocalImport>(Sym)->getChunk());
        LocalImports[Sym] = D;
        continue;
      }
    }

    // We don't want to report missing Microsoft precompiled headers symbols.
    // A proper message will be emitted instead in PDBLinker::aquirePrecompObj
    if (Name.contains("_PchSym_"))
      continue;

    if (Config->MinGW && handleMinGWAutomaticImport(Sym, Name))
      continue;

    // Remaining undefined symbols are not fatal if /force is specified.
    // They are replaced with dummy defined symbols.
    if (Config->ForceUnresolved)
      replaceSymbol<DefinedAbsolute>(Sym, Name, 0);
    Undefs.insert(Sym);
  }

  if (Undefs.empty() && LocalImports.empty())
    return;

  for (Symbol *B : Config->GCRoot) {
    if (Undefs.count(B))
      errorOrWarn("<root>: undefined symbol: " + toString(*B));
    if (Config->WarnLocallyDefinedImported)
      if (Symbol *Imp = LocalImports.lookup(B))
        warn("<root>: locally defined symbol imported: " + toString(*Imp) +
             " (defined in " + toString(Imp->getFile()) + ") [LNK4217]");
  }

  std::vector<UndefinedDiag> UndefDiags;
  DenseMap<Symbol *, int> FirstDiag;

  for (ObjFile *File : ObjFile::Instances) {
    size_t SymIndex = (size_t)-1;
    for (Symbol *Sym : File->getSymbols()) {
      ++SymIndex;
      if (!Sym)
        continue;
      if (Undefs.count(Sym)) {
        auto it = FirstDiag.find(Sym);
        if (it == FirstDiag.end()) {
          FirstDiag[Sym] = UndefDiags.size();
          UndefDiags.push_back({Sym, {{File, SymIndex}}});
        } else {
          UndefDiags[it->second].Files.push_back({File, SymIndex});
        }
      }
      if (Config->WarnLocallyDefinedImported)
        if (Symbol *Imp = LocalImports.lookup(Sym))
          warn(toString(File) +
               ": locally defined symbol imported: " + toString(*Imp) +
               " (defined in " + toString(Imp->getFile()) + ") [LNK4217]");
    }
  }

  for (const UndefinedDiag& UndefDiag : UndefDiags)
    reportUndefinedSymbol(UndefDiag);
}

std::pair<Symbol *, bool> SymbolTable::insert(StringRef Name) {
  bool Inserted = false;
  Symbol *&Sym = SymMap[CachedHashStringRef(Name)];
  if (!Sym) {
    Sym = reinterpret_cast<Symbol *>(make<SymbolUnion>());
    Sym->IsUsedInRegularObj = false;
    Sym->PendingArchiveLoad = false;
    Inserted = true;
  }
  return {Sym, Inserted};
}

std::pair<Symbol *, bool> SymbolTable::insert(StringRef Name, InputFile *File) {
  std::pair<Symbol *, bool> Result = insert(Name);
  if (!File || !isa<BitcodeFile>(File))
    Result.first->IsUsedInRegularObj = true;
  return Result;
}

Symbol *SymbolTable::addUndefined(StringRef Name, InputFile *F,
                                  bool IsWeakAlias) {
  Symbol *S;
  bool WasInserted;
  std::tie(S, WasInserted) = insert(Name, F);
  if (WasInserted || (isa<Lazy>(S) && IsWeakAlias)) {
    replaceSymbol<Undefined>(S, Name);
    return S;
  }
  if (auto *L = dyn_cast<Lazy>(S)) {
    if (!S->PendingArchiveLoad) {
      S->PendingArchiveLoad = true;
      L->File->addMember(&L->Sym);
    }
  }
  return S;
}

void SymbolTable::addLazy(ArchiveFile *F, const Archive::Symbol Sym) {
  StringRef Name = Sym.getName();
  Symbol *S;
  bool WasInserted;
  std::tie(S, WasInserted) = insert(Name);
  if (WasInserted) {
    replaceSymbol<Lazy>(S, F, Sym);
    return;
  }
  auto *U = dyn_cast<Undefined>(S);
  if (!U || U->WeakAlias || S->PendingArchiveLoad)
    return;
  S->PendingArchiveLoad = true;
  F->addMember(&Sym);
}

void SymbolTable::reportDuplicate(Symbol *Existing, InputFile *NewFile) {
  std::string Msg = "duplicate symbol: " + toString(*Existing) + " in " +
                    toString(Existing->getFile()) + " and in " +
                    toString(NewFile);

  if (Config->ForceMultiple)
    warn(Msg);
  else
    error(Msg);
}

Symbol *SymbolTable::addAbsolute(StringRef N, COFFSymbolRef Sym) {
  Symbol *S;
  bool WasInserted;
  std::tie(S, WasInserted) = insert(N, nullptr);
  S->IsUsedInRegularObj = true;
  if (WasInserted || isa<Undefined>(S) || isa<Lazy>(S))
    replaceSymbol<DefinedAbsolute>(S, N, Sym);
  else if (!isa<DefinedCOFF>(S))
    reportDuplicate(S, nullptr);
  return S;
}

Symbol *SymbolTable::addAbsolute(StringRef N, uint64_t VA) {
  Symbol *S;
  bool WasInserted;
  std::tie(S, WasInserted) = insert(N, nullptr);
  S->IsUsedInRegularObj = true;
  if (WasInserted || isa<Undefined>(S) || isa<Lazy>(S))
    replaceSymbol<DefinedAbsolute>(S, N, VA);
  else if (!isa<DefinedCOFF>(S))
    reportDuplicate(S, nullptr);
  return S;
}

Symbol *SymbolTable::addSynthetic(StringRef N, Chunk *C) {
  Symbol *S;
  bool WasInserted;
  std::tie(S, WasInserted) = insert(N, nullptr);
  S->IsUsedInRegularObj = true;
  if (WasInserted || isa<Undefined>(S) || isa<Lazy>(S))
    replaceSymbol<DefinedSynthetic>(S, N, C);
  else if (!isa<DefinedCOFF>(S))
    reportDuplicate(S, nullptr);
  return S;
}

Symbol *SymbolTable::addRegular(InputFile *F, StringRef N,
                                const coff_symbol_generic *Sym,
                                SectionChunk *C) {
  Symbol *S;
  bool WasInserted;
  std::tie(S, WasInserted) = insert(N, F);
  if (WasInserted || !isa<DefinedRegular>(S))
    replaceSymbol<DefinedRegular>(S, F, N, /*IsCOMDAT*/ false,
                                  /*IsExternal*/ true, Sym, C);
  else
    reportDuplicate(S, F);
  return S;
}

std::pair<DefinedRegular *, bool>
SymbolTable::addComdat(InputFile *F, StringRef N,
                       const coff_symbol_generic *Sym) {
  Symbol *S;
  bool WasInserted;
  std::tie(S, WasInserted) = insert(N, F);
  if (WasInserted || !isa<DefinedRegular>(S)) {
    replaceSymbol<DefinedRegular>(S, F, N, /*IsCOMDAT*/ true,
                                  /*IsExternal*/ true, Sym, nullptr);
    return {cast<DefinedRegular>(S), true};
  }
  auto *ExistingSymbol = cast<DefinedRegular>(S);
  if (!ExistingSymbol->isCOMDAT())
    reportDuplicate(S, F);
  return {ExistingSymbol, false};
}

Symbol *SymbolTable::addCommon(InputFile *F, StringRef N, uint64_t Size,
                               const coff_symbol_generic *Sym, CommonChunk *C) {
  Symbol *S;
  bool WasInserted;
  std::tie(S, WasInserted) = insert(N, F);
  if (WasInserted || !isa<DefinedCOFF>(S))
    replaceSymbol<DefinedCommon>(S, F, N, Size, Sym, C);
  else if (auto *DC = dyn_cast<DefinedCommon>(S))
    if (Size > DC->getSize())
      replaceSymbol<DefinedCommon>(S, F, N, Size, Sym, C);
  return S;
}

Symbol *SymbolTable::addImportData(StringRef N, ImportFile *F) {
  Symbol *S;
  bool WasInserted;
  std::tie(S, WasInserted) = insert(N, nullptr);
  S->IsUsedInRegularObj = true;
  if (WasInserted || isa<Undefined>(S) || isa<Lazy>(S)) {
    replaceSymbol<DefinedImportData>(S, N, F);
    return S;
  }

  reportDuplicate(S, F);
  return nullptr;
}

Symbol *SymbolTable::addImportThunk(StringRef Name, DefinedImportData *ID,
                                    uint16_t Machine) {
  Symbol *S;
  bool WasInserted;
  std::tie(S, WasInserted) = insert(Name, nullptr);
  S->IsUsedInRegularObj = true;
  if (WasInserted || isa<Undefined>(S) || isa<Lazy>(S)) {
    replaceSymbol<DefinedImportThunk>(S, Name, ID, Machine);
    return S;
  }

  reportDuplicate(S, ID->File);
  return nullptr;
}

std::vector<Chunk *> SymbolTable::getChunks() {
  std::vector<Chunk *> Res;
  for (ObjFile *File : ObjFile::Instances) {
    ArrayRef<Chunk *> V = File->getChunks();
    Res.insert(Res.end(), V.begin(), V.end());
  }
  return Res;
}

Symbol *SymbolTable::find(StringRef Name) {
  return SymMap.lookup(CachedHashStringRef(Name));
}

Symbol *SymbolTable::findUnderscore(StringRef Name) {
  if (Config->Machine == I386)
    return find(("_" + Name).str());
  return find(Name);
}

// Return all symbols that start with Prefix, possibly ignoring the first
// character of Prefix or the first character symbol.
std::vector<Symbol *> SymbolTable::getSymsWithPrefix(StringRef Prefix) {
  std::vector<Symbol *> Syms;
  for (auto Pair : SymMap) {
    StringRef Name = Pair.first.val();
    if (Name.startswith(Prefix) || Name.startswith(Prefix.drop_front()) ||
        Name.drop_front().startswith(Prefix) ||
        Name.drop_front().startswith(Prefix.drop_front())) {
      Syms.push_back(Pair.second);
    }
  }
  return Syms;
}

Symbol *SymbolTable::findMangle(StringRef Name) {
  if (Symbol *Sym = find(Name))
    if (!isa<Undefined>(Sym))
      return Sym;

  // Efficient fuzzy string lookup is impossible with a hash table, so iterate
  // the symbol table once and collect all possibly matching symbols into this
  // vector. Then compare each possibly matching symbol with each possible
  // mangling.
  std::vector<Symbol *> Syms = getSymsWithPrefix(Name);
  auto FindByPrefix = [&Syms](const Twine &T) -> Symbol * {
    std::string Prefix = T.str();
    for (auto *S : Syms)
      if (S->getName().startswith(Prefix))
        return S;
    return nullptr;
  };

  // For non-x86, just look for C++ functions.
  if (Config->Machine != I386)
    return FindByPrefix("?" + Name + "@@Y");

  if (!Name.startswith("_"))
    return nullptr;
  // Search for x86 stdcall function.
  if (Symbol *S = FindByPrefix(Name + "@"))
    return S;
  // Search for x86 fastcall function.
  if (Symbol *S = FindByPrefix("@" + Name.substr(1) + "@"))
    return S;
  // Search for x86 vectorcall function.
  if (Symbol *S = FindByPrefix(Name.substr(1) + "@@"))
    return S;
  // Search for x86 C++ non-member function.
  return FindByPrefix("?" + Name.substr(1) + "@@Y");
}

Symbol *SymbolTable::addUndefined(StringRef Name) {
  return addUndefined(Name, nullptr, false);
}

std::vector<StringRef> SymbolTable::compileBitcodeFiles() {
  LTO.reset(new BitcodeCompiler);
  for (BitcodeFile *F : BitcodeFile::Instances)
    LTO->add(*F);
  return LTO->compile();
}

void SymbolTable::addCombinedLTOObjects() {
  if (BitcodeFile::Instances.empty())
    return;

  ScopedTimer T(LTOTimer);
  for (StringRef Object : compileBitcodeFiles()) {
    auto *Obj = make<ObjFile>(MemoryBufferRef(Object, "lto.tmp"));
    Obj->parse();
    ObjFile::Instances.push_back(Obj);
  }
}

} // namespace coff
} // namespace lld
