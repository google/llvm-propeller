//===- X86_64.cpp ---------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "InputFiles.h"
#include "OutputSections.h"
#include "Symbols.h"
#include "SyntheticSections.h"
#include "Target.h"
#include "lld/Common/ErrorHandler.h"
#include "llvm/Object/ELF.h"
#include "llvm/Support/Endian.h"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::support::endian;
using namespace llvm::ELF;
using namespace lld;
using namespace lld::elf;

namespace {
class X86_64 : public TargetInfo {
public:
  X86_64();
  int getTlsGdRelaxSkip(RelType Type) const override;
  RelExpr getRelExpr(RelType Type, const Symbol &S,
                     const uint8_t *Loc) const override;
  RelType getDynRel(RelType Type) const override;
  void writeGotPltHeader(uint8_t *Buf) const override;
  void writeGotPlt(uint8_t *Buf, const Symbol &S) const override;
  void writePltHeader(uint8_t *Buf) const override;
  void writePlt(uint8_t *Buf, uint64_t GotPltEntryAddr, uint64_t PltEntryAddr,
                int32_t Index, unsigned RelOff) const override;
  void relocateOne(uint8_t *Loc, RelType Type, uint64_t Val) const override;
  void relocateOneJumpRelocation(uint8_t *Loc, JumpRelType Type,
                                 JumpRelType Val) const override;

  RelExpr adjustRelaxExpr(RelType Type, const uint8_t *Data,
                          RelExpr Expr) const override;
  void relaxGot(uint8_t *Loc, RelType Type, uint64_t Val) const override;
  void relaxTlsGdToIe(uint8_t *Loc, RelType Type, uint64_t Val) const override;
  void relaxTlsGdToLe(uint8_t *Loc, RelType Type, uint64_t Val) const override;
  void relaxTlsIeToLe(uint8_t *Loc, RelType Type, uint64_t Val) const override;
  void relaxTlsLdToLe(uint8_t *Loc, RelType Type, uint64_t Val) const override;
  bool adjustPrologueForCrossSplitStack(uint8_t *Loc, uint8_t *End,
                                        uint8_t StOther) const override;

  bool deleteFallThruJmpInsn(InputSection &IS, InputFile *File,
                             InputSection *NextIS) const override;

  unsigned shrinkJmpInsn(InputSection &IS, InputFile *File) const override;

  unsigned growJmpInsn(InputSection &IS, InputFile *File) const override;
};
} // namespace

static std::vector<std::vector<uint8_t>> X86_NOP_INSTRUCTIONS = {
  {0x66, 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00},
  {0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00},
  {0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00},
  {0x66, 0x0f, 0x1f, 0x44, 0x00, 0x00},
  {0x0f, 0x1f, 0x44, 0x00, 0x00},
  {0x0f, 0x1f, 0x00},
  {0x66, 0x90},
  {0x90}
};

X86_64::X86_64() {
  CopyRel = R_X86_64_COPY;
  GotRel = R_X86_64_GLOB_DAT;
  NoneRel = R_X86_64_NONE;
  PltRel = R_X86_64_JUMP_SLOT;
  RelativeRel = R_X86_64_RELATIVE;
  IRelativeRel = R_X86_64_IRELATIVE;
  SymbolicRel = R_X86_64_64;
  TlsDescRel = R_X86_64_TLSDESC;
  TlsGotRel = R_X86_64_TPOFF64;
  TlsModuleIndexRel = R_X86_64_DTPMOD64;
  TlsOffsetRel = R_X86_64_DTPOFF64;
  PltEntrySize = 16;
  PltHeaderSize = 16;
  TrapInstr = {0xcc, 0xcc, 0xcc, 0xcc}; // 0xcc = INT3

  // Align to the large page size (known as a superpage or huge page).
  // FreeBSD automatically promotes large, superpage-aligned allocations.
  DefaultImageBase = 0x200000;
}

int X86_64::getTlsGdRelaxSkip(RelType Type) const { return 2; }

// Opcodes for the different X86_64 jmp instructions.
enum JmpInsnOpcode {
  J_JMP_32,
  J_JNE_32,
  J_JE_32,
  J_JG_32,
  J_JGE_32,
  J_JB_32,
  J_JBE_32,
  J_JL_32,
  J_JLE_32,
  J_JA_32,
  J_JAE_32,
  J_UNKNOWN,
};

// Given the first (optional) and second byte of the insn's opcode, this
// returns the corresponding enum value.
static JmpInsnOpcode getJmpInsnType(const uint8_t *First,
                                    const uint8_t *Second) {
  if (*Second  == 0xe9)
      return J_JMP_32;

  if (First == nullptr)
    return J_UNKNOWN;

  if (*First == 0x0f) {
    switch (*Second) {
      case 0x84:
        return J_JE_32;
      case 0x85:
        return J_JNE_32;
      case 0x8f:
        return J_JG_32;
      case 0x8d:
        return J_JGE_32;
      case 0x82:
        return J_JB_32;
      case 0x86:
        return J_JBE_32;
      case 0x8c:
        return J_JL_32;
      case 0x8e:
        return J_JLE_32;
      case 0x87:
        return J_JA_32;
      case 0x83:
        return J_JAE_32;
    }
  }
  return J_UNKNOWN;
}

static unsigned getRelocationWithOffset(const InputSection &IS,
                                        uint64_t Offset) {
  unsigned I = 0;
  for (; I < IS.Relocations.size(); ++I) {
    if (IS.Relocations[I].Offset == Offset &&
        IS.Relocations[I].Expr != R_NONE)
      break;
  }
  return I;
}

static unsigned getJumpRelocationWithOffset(const InputSection &IS,
                                            uint64_t Offset) {
  unsigned I = 0;
  for (; I < IS.JumpRelocations.size(); ++I) {
    if (IS.JumpRelocations[I].Offset == Offset)
      break;
  }
  return I;
}

static bool isRelocationForJmpInsn(Relocation &R) {
  return (R.Type == R_X86_64_PLT32 || R.Type == R_X86_64_PC32 || R.Type == R_X86_64_PC8);
}

static bool isDirectJmpInsnOpcode(const uint8_t *Opcode) {
  return (*Opcode == 0xe9);
}


// Return true if Relocaction R points to the first instruction in the
// next section.
static bool isFallThruRelocation(InputSection &IS, InputFile *File,
                                 InputSection *NextIS, Relocation &R) {
  if (!isRelocationForJmpInsn(R))
    return false;

  uint64_t AddrLoc = (IS.getOutputSection())->Addr + IS.OutSecOff + R.Offset;
  uint64_t TargetOffset = SignExtend64(
      InputSectionBase::getRelocTargetVA(File, R.Type, R.Addend,
                                         AddrLoc, *R.Sym, R.Expr),
      (Config->Wordsize * 8));

  // If this jmp is a fall thru, the target offset is the beginning of the
  // next section.
  uint64_t NextSectionOffset = NextIS->getOutputSection()->Addr +
                               NextIS->OutSecOff;
  if ((AddrLoc + 4 + TargetOffset) != NextSectionOffset)
    return false;

  return true;
}

// Return the jmp instruction opcode that is the inverse of the given
// opcode.  For example, JE inverted is JNE.
static JmpInsnOpcode invertJmpOpcode(const JmpInsnOpcode opcode) {
  switch(opcode) {
    case J_JE_32:
      return J_JNE_32;
    case J_JNE_32:
      return J_JE_32;
    case J_JG_32:
      return J_JLE_32;
    case J_JGE_32:
      return J_JL_32;
    case J_JB_32:
      return J_JAE_32;
    case J_JBE_32:
      return J_JA_32;
    case J_JL_32:
      return J_JGE_32;
    case J_JLE_32:
      return J_JG_32;
    case J_JA_32:
      return J_JBE_32;
    case J_JAE_32:
      return J_JB_32;
    default:
      return J_UNKNOWN;
  }
  return J_UNKNOWN;
}

bool X86_64::deleteFallThruJmpInsn(InputSection &IS, InputFile *File,
                                   InputSection *NextIS) const {
  const unsigned SizeOfDirectJmpInsn = 5;

  if (NextIS == nullptr)
    return false;

  if (IS.getSize() < SizeOfDirectJmpInsn)
    return false;

  // If this jmp insn can be removed, it is the last insn and the
  // relocation is 4 bytes before the end.
  unsigned RIndex = getRelocationWithOffset(IS, (IS.getSize() - 4));
  if (RIndex == IS.Relocations.size())
    return false;

  Relocation &R = IS.Relocations[RIndex];

  // Check if the relocation corresponds to a direct jmp.
  const uint8_t *SecContents = IS.data().data();
  if (!isDirectJmpInsnOpcode(SecContents + R.Offset - 1))
    return false;

  if (isFallThruRelocation(IS, File, NextIS, R)) {
    // This is a fall thru and can be deleted.
    R.Expr = R_NONE;
    R.Offset = 0;
    IS.drop_back(SizeOfDirectJmpInsn);
    //IS.Filler =  {0x90, 0x90, 0x90, 0x90};
    IS.SpecialFiller = X86_NOP_INSTRUCTIONS;
    return true;
  }

  // Now, check if flip and delete is possible.
  const unsigned SizeOfJmpCCInsn = 6;
  // To flip, there must be atleast one JmpCC and one direct jmp.
  if (IS.getSize() < (SizeOfDirectJmpInsn + SizeOfJmpCCInsn)) return 0;

  unsigned RbIndex = getRelocationWithOffset(IS,
                         (IS.getSize() - SizeOfDirectJmpInsn - 4));
  if (RbIndex == IS.Relocations.size()) return 0;

  Relocation &Rb = IS.Relocations[RbIndex];

  const uint8_t *JmpInsnB = SecContents + Rb.Offset - 1;
  JmpInsnOpcode JO_B = getJmpInsnType(JmpInsnB - 1, JmpInsnB);
  if (JO_B == J_UNKNOWN)
    return false;

  if (!isFallThruRelocation(IS, File, NextIS, Rb))
    return false;

  // jmpCC jumps to the fall thru block, the branch can be flipped and the
  // jmp can be deleted.
  JmpInsnOpcode JInvert = invertJmpOpcode(JO_B);
  if (JInvert == J_UNKNOWN)
    return false;
  IS.addJumpRelocation({JInvert, (Rb.Offset - 1), 4});
  // Move R's values to Rb
  Rb.Expr = R.Expr;
  Rb.Type = R.Type;
  Rb.Addend = R.Addend;
  Rb.Sym = R.Sym;
  // Cancel R
  R.Expr = R_NONE;
  R.Offset = 0;
  IS.drop_back(SizeOfDirectJmpInsn);
  //IS.Filler =  {0x90, 0x90, 0x90, 0x90};
  IS.SpecialFiller = X86_NOP_INSTRUCTIONS;
  return true;
}

// Returns target offset if the Relocation R corresponds to a jmp instruction
// and the offset of the relocation is 1 byte wide.
static uint64_t getTargetOffsetForJmp(InputSection &IS, InputFile *File,
                                      Relocation &R, JmpInsnOpcode &JmpCode) {
  const unsigned SizeOfJmpCCOpcode = 2;

  if (!isRelocationForJmpInsn(R)){
    return false;
  }

  unsigned JIndex = getJumpRelocationWithOffset(IS, (R.Offset - 1));
  if (JIndex != IS.JumpRelocations.size()){
    JmpCode = static_cast<JmpInsnOpcode>(IS.JumpRelocations[JIndex].Original);
  } else {
    const uint8_t *SecContents = IS.data().data();
    const uint8_t *JmpInsn = SecContents + R.Offset - 1;
    const uint8_t *JmpCCInsn = (R.Offset >= SizeOfJmpCCOpcode) ?
                               (JmpInsn - 1) : nullptr;
    JmpCode = getJmpInsnType(JmpCCInsn, JmpInsn);
  }
  if (JmpCode == J_UNKNOWN){
    return 0;
  }

  uint64_t AddrLoc = (IS.getOutputSection())->Addr + IS.OutSecOff + R.Offset;
  uint64_t TargetOffset = SignExtend64(
      InputSectionBase::getRelocTargetVA(File, R.Type, R.Addend,
                                         AddrLoc, *R.Sym, R.Expr),
      (Config->Wordsize * 8));

  return TargetOffset;
}

static bool isOneByteOffsetWhenShrunk(uint64_t TargetOffset,
                                         JmpInsnOpcode JmpCode,
                                         unsigned BytesShrunk) {
  // For negative jumps, the jump target will be closer if shrinking
  // is done.
  if ((int64_t) TargetOffset < 0){
    TargetOffset += BytesShrunk;
    TargetOffset += (JmpCode == J_JMP_32) ? 3 : 4;
  }
  return ((int64_t)TargetOffset == llvm::SignExtend64(TargetOffset, 8));
}

static bool isOneByteOffset(uint64_t TargetOffset, unsigned BytesGrown) {
  // For negative jumps, the jump target is further.
  if ((int64_t) TargetOffset < 0){
    TargetOffset -= BytesGrown;
  }
  return ((int64_t)TargetOffset == llvm::SignExtend64(TargetOffset, 8));
}



static void shrinkJmpWithRelocation(InputSection &IS, JmpInsnOpcode JmpCode,
                                     Relocation &R, unsigned &BytesShrunk,
                                     bool DoShrinkJmp = true) {
  // Check if there is a Jump Relocation against this offset.
  unsigned JIndex = getJumpRelocationWithOffset(IS, (R.Offset - 1));

  if (DoShrinkJmp && JmpCode!=J_JMP_32)
    BytesShrunk += 1;

  // Update R.Offset
  R.Offset -= BytesShrunk;
  unsigned NewJmpSize = DoShrinkJmp ? 1 : 4;

  if (JIndex < IS.JumpRelocations.size()) {
    JumpRelocation &J = IS.JumpRelocations[JIndex];
    assert((!DoShrinkJmp || J.Size == 4) && "Not the right size of jump.");
    J.Offset = R.Offset - 1;
    if (DoShrinkJmp)
      J.Size = NewJmpSize;
  } else {
    IS.addJumpRelocation({JmpCode, R.Offset - 1, NewJmpSize});
  }

  if (DoShrinkJmp) {
    // Shrinking Jmp corresponding to relocation R, adjust type and addend.
    R.Type = R_X86_64_PC8;
    assert(R.Addend == -4 && "Addend must be -4 to shrink.");
    R.Addend += 3;
    BytesShrunk += 3;
  }
}

unsigned X86_64::shrinkJmpInsn(InputSection &IS, InputFile *File) const {
  const unsigned SizeOfDirectShortJmpInsn = 2;
  const unsigned SizeOfDirectNearJmpInsn = 5;
  const unsigned SizeOfJmpCCInsn = 6;
  int SizeOfDirectJmpInsn = SizeOfDirectNearJmpInsn;

  bool IsShortJmp = false;

  if (IS.getSize() < SizeOfDirectNearJmpInsn){
    return 0;
  }

  unsigned RIndex = getRelocationWithOffset(IS, (IS.getSize() - 4));

  if (RIndex == IS.Relocations.size()){
    RIndex = getRelocationWithOffset(IS, (IS.getSize() - 1));
    if (RIndex == IS.Relocations.size()) {
      return 0;
    }

    SizeOfDirectJmpInsn = SizeOfDirectShortJmpInsn;
    IsShortJmp = true;
  }

  Relocation &R = IS.Relocations[RIndex];

  JmpInsnOpcode JmpCode = J_UNKNOWN;

  uint64_t TargetOffset = getTargetOffsetForJmp(IS, File, R, JmpCode);
  bool DirectJmp = (JmpCode == J_JMP_32);

  if (JmpCode == J_UNKNOWN) {
    return 0;
  }

  unsigned BytesShrunk = 0;

  if (!DirectJmp) {
    if (!IsShortJmp && isOneByteOffsetWhenShrunk(TargetOffset, JmpCode, BytesShrunk)) {
      shrinkJmpWithRelocation(IS, JmpCode, R, BytesShrunk);
    }
  } else {
    // For Direct Jmps, the previous insn might be a jmpcc that can be
    // shrinked.  Check that also.
    if (IS.getSize() >= (SizeOfDirectJmpInsn + SizeOfJmpCCInsn)) {
      unsigned RbIndex = getRelocationWithOffset(
          IS, (IS.getSize() - SizeOfDirectJmpInsn - 4));

      if (RbIndex != IS.Relocations.size()) {
        Relocation &Rb = IS.Relocations[RbIndex];
        JmpInsnOpcode JmpCode_B = J_UNKNOWN;
        uint64_t TargetOffset_B = getTargetOffsetForJmp(IS, File, Rb, JmpCode_B);
        if (JmpCode_B != J_UNKNOWN && JmpCode_B != J_JMP_32
            && isOneByteOffsetWhenShrunk(TargetOffset_B, JmpCode, BytesShrunk)) {
          shrinkJmpWithRelocation(IS, JmpCode_B, Rb, BytesShrunk);
        }
      }
    }
    bool CanShrinkR = !IsShortJmp && isOneByteOffsetWhenShrunk(TargetOffset, JmpCode, BytesShrunk);
    shrinkJmpWithRelocation(IS, JmpCode, R, BytesShrunk, CanShrinkR);
  }

  if (BytesShrunk) {
    IS.drop_back(BytesShrunk);
  }
  return BytesShrunk;
}

static void growJmpWithRelocation(InputSection &IS, JmpInsnOpcode JmpCode,
                                     Relocation &R, unsigned &BytesGrown,
                                     bool DoGrowJmp = true) {
  // Check if there is a Jump Relocation against this offset.
  unsigned JIndex = getJumpRelocationWithOffset(IS, (R.Offset - 1));

  if (JIndex == IS.JumpRelocations.size()){
    error("Jump relocation does not exist!");
    return;
  }

  if (DoGrowJmp && JmpCode!=J_JMP_32)
    BytesGrown += 1;

  // Update R.Offset
  R.Offset += BytesGrown;

  JumpRelocation &J = IS.JumpRelocations[JIndex];
  assert((!DoGrowJmp || J.Size == 1) && "Not the right size of jump.");
  J.Offset = R.Offset - 1;
  if (DoGrowJmp) {
    // Growing Jmp corresponding to relocation R, adjust type and addend.
    J.Size = 4;
    R.Type = R_X86_64_PC32;
    assert(R.Addend == -1 && "Addend must be -1 to grow.");
    R.Addend -= 3;
    BytesGrown += 3;
  }
}

unsigned X86_64::growJmpInsn(InputSection &IS, InputFile *File) const {
  const unsigned SizeOfJmpCCInsn = 2;
  const unsigned SizeOfDirectNearJmpInsn = 5;
  const unsigned SizeOfDirectShortJmpInsn = 2;
  int SizeOfDirectJmpInsn = SizeOfDirectShortJmpInsn;

  if (IS.getSize() < SizeOfDirectShortJmpInsn)
    return 0;

  bool IsShortJmp = true;

  unsigned RIndex = getRelocationWithOffset(IS, (IS.getSize() - 1));

  if (RIndex == IS.Relocations.size()){
    if (IS.getSize() < SizeOfDirectNearJmpInsn){
      return 0;
    }

    RIndex = getRelocationWithOffset(IS, (IS.getSize() - 4));
    if (RIndex == IS.Relocations.size()){
      return 0;
    }
    IsShortJmp = false;
    SizeOfDirectJmpInsn = SizeOfDirectNearJmpInsn;
  }

  Relocation &R = IS.Relocations[RIndex];

  JmpInsnOpcode JmpCode = J_UNKNOWN;

  uint64_t TargetOffset = getTargetOffsetForJmp(IS, File, R, JmpCode);
  bool DirectJmp = (JmpCode == J_JMP_32);

  if (JmpCode == J_UNKNOWN) {
    return 0;
  }

  unsigned BytesGrown = 0;
  if (!DirectJmp) {
    // Grow JmpInsn.
    if (IsShortJmp && !isOneByteOffset(TargetOffset, BytesGrown)){
      growJmpWithRelocation(IS, JmpCode, R, BytesGrown);
    }
  } else {
    // For Direct Jmps, the previous insn might be a jmpcc that might need
    // to be grown.  Check that also.
    if (IS.getSize() >= (SizeOfDirectJmpInsn + SizeOfJmpCCInsn)) {
      unsigned RbIndex = getRelocationWithOffset(
          IS, (IS.getSize() - SizeOfDirectJmpInsn - 1));

      if (RbIndex != IS.Relocations.size()) {
        Relocation &Rb = IS.Relocations[RbIndex];
        JmpInsnOpcode JmpCode_B = J_UNKNOWN;
        uint64_t TargetOffset_B = getTargetOffsetForJmp(IS, File, Rb, JmpCode_B);
        if (JmpCode_B != J_UNKNOWN && JmpCode_B != J_JMP_32
            && !isOneByteOffset(TargetOffset_B, BytesGrown)) {
          growJmpWithRelocation(IS, JmpCode_B, Rb, BytesGrown);
        }
      }
    }
    bool ShouldGrowR = IsShortJmp && !isOneByteOffset(TargetOffset, BytesGrown);
    growJmpWithRelocation(IS, JmpCode, R, BytesGrown, ShouldGrowR);
  }

  if (BytesGrown) {
    IS.push_back(BytesGrown);
  }
  return BytesGrown;
}


RelExpr X86_64::getRelExpr(RelType Type, const Symbol &S,
                           const uint8_t *Loc) const {
  if (Type == R_X86_64_GOTTPOFF)
    Config->HasStaticTlsModel = true;

  switch (Type) {
  case R_X86_64_8:
  case R_X86_64_16:
  case R_X86_64_32:
  case R_X86_64_32S:
  case R_X86_64_64:
    return R_ABS;
  case R_X86_64_DTPOFF32:
  case R_X86_64_DTPOFF64:
    return R_DTPREL;
  case R_X86_64_TPOFF32:
    return R_TLS;
  case R_X86_64_TLSDESC_CALL:
    return R_TLSDESC_CALL;
  case R_X86_64_TLSLD:
    return R_TLSLD_PC;
  case R_X86_64_TLSGD:
    return R_TLSGD_PC;
  case R_X86_64_SIZE32:
  case R_X86_64_SIZE64:
    return R_SIZE;
  case R_X86_64_PLT32:
    return R_PLT_PC;
  case R_X86_64_PC8:
  case R_X86_64_PC16:
  case R_X86_64_PC32:
  case R_X86_64_PC64:
    return R_PC;
  case R_X86_64_GOT32:
  case R_X86_64_GOT64:
    return R_GOTPLT;
  case R_X86_64_GOTPC32_TLSDESC:
    return R_TLSDESC_PC;
  case R_X86_64_GOTPCREL:
  case R_X86_64_GOTPCRELX:
  case R_X86_64_REX_GOTPCRELX:
  case R_X86_64_GOTTPOFF:
    return R_GOT_PC;
  case R_X86_64_GOTOFF64:
    return R_GOTPLTREL;
  case R_X86_64_GOTPC32:
  case R_X86_64_GOTPC64:
    return R_GOTPLTONLY_PC;
  case R_X86_64_NONE:
    return R_NONE;
  default:
    error(getErrorLocation(Loc) + "unknown relocation (" + Twine(Type) +
          ") against symbol " + toString(S));
    return R_NONE;
  }
}

void X86_64::writeGotPltHeader(uint8_t *Buf) const {
  // The first entry holds the value of _DYNAMIC. It is not clear why that is
  // required, but it is documented in the psabi and the glibc dynamic linker
  // seems to use it (note that this is relevant for linking ld.so, not any
  // other program).
  write64le(Buf, Main->Dynamic->getVA());
}

void X86_64::writeGotPlt(uint8_t *Buf, const Symbol &S) const {
  // See comments in X86::writeGotPlt.
  write64le(Buf, S.getPltVA() + 6);
}

void X86_64::writePltHeader(uint8_t *Buf) const {
  const uint8_t PltData[] = {
      0xff, 0x35, 0, 0, 0, 0, // pushq GOTPLT+8(%rip)
      0xff, 0x25, 0, 0, 0, 0, // jmp *GOTPLT+16(%rip)
      0x0f, 0x1f, 0x40, 0x00, // nop
  };
  memcpy(Buf, PltData, sizeof(PltData));
  uint64_t GotPlt = In.GotPlt->getVA();
  uint64_t Plt = In.Plt->getVA();
  write32le(Buf + 2, GotPlt - Plt + 2); // GOTPLT+8
  write32le(Buf + 8, GotPlt - Plt + 4); // GOTPLT+16
}

void X86_64::writePlt(uint8_t *Buf, uint64_t GotPltEntryAddr,
                      uint64_t PltEntryAddr, int32_t Index,
                      unsigned RelOff) const {
  const uint8_t Inst[] = {
      0xff, 0x25, 0, 0, 0, 0, // jmpq *got(%rip)
      0x68, 0, 0, 0, 0,       // pushq <relocation index>
      0xe9, 0, 0, 0, 0,       // jmpq plt[0]
  };
  memcpy(Buf, Inst, sizeof(Inst));

  write32le(Buf + 2, GotPltEntryAddr - PltEntryAddr - 6);
  write32le(Buf + 7, Index);
  write32le(Buf + 12, -PltHeaderSize - PltEntrySize * Index - 16);
}

RelType X86_64::getDynRel(RelType Type) const {
  if (Type == R_X86_64_64 || Type == R_X86_64_PC64 || Type == R_X86_64_SIZE32 ||
      Type == R_X86_64_SIZE64)
    return Type;
  return R_X86_64_NONE;
}

void X86_64::relaxTlsGdToLe(uint8_t *Loc, RelType Type, uint64_t Val) const {
  if (Type == R_X86_64_TLSGD) {
    // Convert
    //   .byte 0x66
    //   leaq x@tlsgd(%rip), %rdi
    //   .word 0x6666
    //   rex64
    //   call __tls_get_addr@plt
    // to the following two instructions.
    const uint8_t Inst[] = {
        0x64, 0x48, 0x8b, 0x04, 0x25, 0x00, 0x00,
        0x00, 0x00,                            // mov %fs:0x0,%rax
        0x48, 0x8d, 0x80, 0,    0,    0,    0, // lea x@tpoff,%rax
    };
    memcpy(Loc - 4, Inst, sizeof(Inst));

    // The original code used a pc relative relocation and so we have to
    // compensate for the -4 in had in the addend.
    write32le(Loc + 8, Val + 4);
  } else {
    // Convert
    //   lea x@tlsgd(%rip), %rax
    //   call *(%rax)
    // to the following two instructions.
    assert(Type == R_X86_64_GOTPC32_TLSDESC);
    if (memcmp(Loc - 3, "\x48\x8d\x05", 3)) {
      error(getErrorLocation(Loc - 3) + "R_X86_64_GOTPC32_TLSDESC must be used "
                                        "in callq *x@tlsdesc(%rip), %rax");
      return;
    }
    // movq $x@tpoff(%rip),%rax
    Loc[-2] = 0xc7;
    Loc[-1] = 0xc0;
    write32le(Loc, Val + 4);
    // xchg ax,ax
    Loc[4] = 0x66;
    Loc[5] = 0x90;
  }
}

void X86_64::relaxTlsGdToIe(uint8_t *Loc, RelType Type, uint64_t Val) const {
  if (Type == R_X86_64_TLSGD) {
    // Convert
    //   .byte 0x66
    //   leaq x@tlsgd(%rip), %rdi
    //   .word 0x6666
    //   rex64
    //   call __tls_get_addr@plt
    // to the following two instructions.
    const uint8_t Inst[] = {
        0x64, 0x48, 0x8b, 0x04, 0x25, 0x00, 0x00,
        0x00, 0x00,                            // mov %fs:0x0,%rax
        0x48, 0x03, 0x05, 0,    0,    0,    0, // addq x@gottpoff(%rip),%rax
    };
    memcpy(Loc - 4, Inst, sizeof(Inst));

    // Both code sequences are PC relatives, but since we are moving the
    // constant forward by 8 bytes we have to subtract the value by 8.
    write32le(Loc + 8, Val - 8);
  } else {
    // Convert
    //   lea x@tlsgd(%rip), %rax
    //   call *(%rax)
    // to the following two instructions.
    assert(Type == R_X86_64_GOTPC32_TLSDESC);
    if (memcmp(Loc - 3, "\x48\x8d\x05", 3)) {
      error(getErrorLocation(Loc - 3) + "R_X86_64_GOTPC32_TLSDESC must be used "
                                        "in callq *x@tlsdesc(%rip), %rax");
      return;
    }
    // movq x@gottpoff(%rip),%rax
    Loc[-2] = 0x8b;
    write32le(Loc, Val);
    // xchg ax,ax
    Loc[4] = 0x66;
    Loc[5] = 0x90;
  }
}

// In some conditions, R_X86_64_GOTTPOFF relocation can be optimized to
// R_X86_64_TPOFF32 so that it does not use GOT.
void X86_64::relaxTlsIeToLe(uint8_t *Loc, RelType Type, uint64_t Val) const {
  uint8_t *Inst = Loc - 3;
  uint8_t Reg = Loc[-1] >> 3;
  uint8_t *RegSlot = Loc - 1;

  // Note that ADD with RSP or R12 is converted to ADD instead of LEA
  // because LEA with these registers needs 4 bytes to encode and thus
  // wouldn't fit the space.

  if (memcmp(Inst, "\x48\x03\x25", 3) == 0) {
    // "addq foo@gottpoff(%rip),%rsp" -> "addq $foo,%rsp"
    memcpy(Inst, "\x48\x81\xc4", 3);
  } else if (memcmp(Inst, "\x4c\x03\x25", 3) == 0) {
    // "addq foo@gottpoff(%rip),%r12" -> "addq $foo,%r12"
    memcpy(Inst, "\x49\x81\xc4", 3);
  } else if (memcmp(Inst, "\x4c\x03", 2) == 0) {
    // "addq foo@gottpoff(%rip),%r[8-15]" -> "leaq foo(%r[8-15]),%r[8-15]"
    memcpy(Inst, "\x4d\x8d", 2);
    *RegSlot = 0x80 | (Reg << 3) | Reg;
  } else if (memcmp(Inst, "\x48\x03", 2) == 0) {
    // "addq foo@gottpoff(%rip),%reg -> "leaq foo(%reg),%reg"
    memcpy(Inst, "\x48\x8d", 2);
    *RegSlot = 0x80 | (Reg << 3) | Reg;
  } else if (memcmp(Inst, "\x4c\x8b", 2) == 0) {
    // "movq foo@gottpoff(%rip),%r[8-15]" -> "movq $foo,%r[8-15]"
    memcpy(Inst, "\x49\xc7", 2);
    *RegSlot = 0xc0 | Reg;
  } else if (memcmp(Inst, "\x48\x8b", 2) == 0) {
    // "movq foo@gottpoff(%rip),%reg" -> "movq $foo,%reg"
    memcpy(Inst, "\x48\xc7", 2);
    *RegSlot = 0xc0 | Reg;
  } else {
    error(getErrorLocation(Loc - 3) +
          "R_X86_64_GOTTPOFF must be used in MOVQ or ADDQ instructions only");
  }

  // The original code used a PC relative relocation.
  // Need to compensate for the -4 it had in the addend.
  write32le(Loc, Val + 4);
}

void X86_64::relaxTlsLdToLe(uint8_t *Loc, RelType Type, uint64_t Val) const {
  if (Type == R_X86_64_DTPOFF64) {
    write64le(Loc, Val);
    return;
  }
  if (Type == R_X86_64_DTPOFF32) {
    write32le(Loc, Val);
    return;
  }

  const uint8_t Inst[] = {
      0x66, 0x66,                                           // .word 0x6666
      0x66,                                                 // .byte 0x66
      0x64, 0x48, 0x8b, 0x04, 0x25, 0x00, 0x00, 0x00, 0x00, // mov %fs:0,%rax
  };

  if (Loc[4] == 0xe8) {
    // Convert
    //   leaq bar@tlsld(%rip), %rdi           # 48 8d 3d <Loc>
    //   callq __tls_get_addr@PLT             # e8 <disp32>
    //   leaq bar@dtpoff(%rax), %rcx
    // to
    //   .word 0x6666
    //   .byte 0x66
    //   mov %fs:0,%rax
    //   leaq bar@tpoff(%rax), %rcx
    memcpy(Loc - 3, Inst, sizeof(Inst));
    return;
  }

  if (Loc[4] == 0xff && Loc[5] == 0x15) {
    // Convert
    //   leaq  x@tlsld(%rip),%rdi               # 48 8d 3d <Loc>
    //   call *__tls_get_addr@GOTPCREL(%rip)    # ff 15 <disp32>
    // to
    //   .long  0x66666666
    //   movq   %fs:0,%rax
    // See "Table 11.9: LD -> LE Code Transition (LP64)" in
    // https://raw.githubusercontent.com/wiki/hjl-tools/x86-psABI/x86-64-psABI-1.0.pdf
    Loc[-3] = 0x66;
    memcpy(Loc - 2, Inst, sizeof(Inst));
    return;
  }

  error(getErrorLocation(Loc - 3) +
        "expected R_X86_64_PLT32 or R_X86_64_GOTPCRELX after R_X86_64_TLSLD");
}

void X86_64::relocateOneJumpRelocation(uint8_t *Loc, JumpRelType Type,
                                       unsigned Size) const {
  switch(Type) {
  case J_JMP_32:
    if (Size == 4) *Loc = 0xe9; else *Loc  = 0xeb;
    break;
  case J_JE_32:
    if (Size == 4) {*(Loc-1) = 0x0f; *Loc = 0x84; } else *Loc  = 0x74;
    break;
  case J_JNE_32:
    if (Size == 4) {*(Loc-1) = 0x0f; *Loc = 0x85;} else *Loc  = 0x75;
    break;
  case J_JG_32:
    if (Size == 4) {*(Loc-1) = 0x0f; *Loc = 0x8f;} else *Loc  = 0x7f;
    break;
  case J_JGE_32:
    if (Size == 4) {*(Loc-1) = 0x0f; *Loc = 0x8d;} else *Loc  = 0x7d;
    break;
  case J_JB_32:
    if (Size == 4) {*(Loc-1) = 0x0f; *Loc = 0x82;} else *Loc  = 0x72;
    break;
  case J_JBE_32:
    if (Size == 4) {*(Loc-1) = 0x0f; *Loc = 0x86;} else *Loc  = 0x76;
    break;
  case J_JL_32:
    if (Size == 4) {*(Loc-1) = 0x0f; *Loc = 0x8c;} else *Loc  = 0x7c;
    break;
  case J_JLE_32:
    if (Size == 4) {*(Loc-1) = 0x0f; *Loc = 0x8e;} else *Loc  = 0x7e;
    break;
  case J_JA_32:
    if (Size == 4) {*(Loc-1) = 0x0f ; *Loc = 0x87;} else *Loc  = 0x77;
    break;
  case J_JAE_32:
    if (Size == 4) {*(Loc-1) = 0x0f ; *Loc = 0x83;} else *Loc  = 0x73;
    break;
  default:
    error(getErrorLocation(Loc) + "unrecognized jump reloc " + Twine(Type));
  }
}

void X86_64::relocateOne(uint8_t *Loc, RelType Type, uint64_t Val) const {
  switch (Type) {
  case R_X86_64_8:
    checkIntUInt(Loc, Val, 8, Type);
    *Loc = Val;
    break;
  case R_X86_64_PC8:
    checkInt(Loc, Val, 8, Type);
    *Loc = Val;
    break;
  case R_X86_64_16:
    checkIntUInt(Loc, Val, 16, Type);
    write16le(Loc, Val);
    break;
  case R_X86_64_PC16:
    checkInt(Loc, Val, 16, Type);
    write16le(Loc, Val);
    break;
  case R_X86_64_32:
    checkUInt(Loc, Val, 32, Type);
    write32le(Loc, Val);
    break;
  case R_X86_64_32S:
  case R_X86_64_TPOFF32:
  case R_X86_64_GOT32:
  case R_X86_64_GOTPC32:
  case R_X86_64_GOTPC32_TLSDESC:
  case R_X86_64_GOTPCREL:
  case R_X86_64_GOTPCRELX:
  case R_X86_64_REX_GOTPCRELX:
  case R_X86_64_PC32:
  case R_X86_64_GOTTPOFF:
  case R_X86_64_PLT32:
  case R_X86_64_TLSGD:
  case R_X86_64_TLSLD:
  case R_X86_64_DTPOFF32:
  case R_X86_64_SIZE32:
    checkInt(Loc, Val, 32, Type);
    write32le(Loc, Val);
    break;
  case R_X86_64_64:
  case R_X86_64_DTPOFF64:
  case R_X86_64_PC64:
  case R_X86_64_SIZE64:
  case R_X86_64_GOT64:
  case R_X86_64_GOTOFF64:
  case R_X86_64_GOTPC64:
    write64le(Loc, Val);
    break;
  default:
    llvm_unreachable("unknown relocation");
  }
}

RelExpr X86_64::adjustRelaxExpr(RelType Type, const uint8_t *Data,
                                RelExpr RelExpr) const {
  if (Type != R_X86_64_GOTPCRELX && Type != R_X86_64_REX_GOTPCRELX)
    return RelExpr;
  const uint8_t Op = Data[-2];
  const uint8_t ModRm = Data[-1];

  // FIXME: When PIC is disabled and foo is defined locally in the
  // lower 32 bit address space, memory operand in mov can be converted into
  // immediate operand. Otherwise, mov must be changed to lea. We support only
  // latter relaxation at this moment.
  if (Op == 0x8b)
    return R_RELAX_GOT_PC;

  // Relax call and jmp.
  if (Op == 0xff && (ModRm == 0x15 || ModRm == 0x25))
    return R_RELAX_GOT_PC;

  // Relaxation of test, adc, add, and, cmp, or, sbb, sub, xor.
  // If PIC then no relaxation is available.
  // We also don't relax test/binop instructions without REX byte,
  // they are 32bit operations and not common to have.
  assert(Type == R_X86_64_REX_GOTPCRELX);
  return Config->Pic ? RelExpr : R_RELAX_GOT_PC_NOPIC;
}

// A subset of relaxations can only be applied for no-PIC. This method
// handles such relaxations. Instructions encoding information was taken from:
// "Intel 64 and IA-32 Architectures Software Developer's Manual V2"
// (http://www.intel.com/content/dam/www/public/us/en/documents/manuals/
//    64-ia-32-architectures-software-developer-instruction-set-reference-manual-325383.pdf)
static void relaxGotNoPic(uint8_t *Loc, uint64_t Val, uint8_t Op,
                          uint8_t ModRm) {
  const uint8_t Rex = Loc[-3];
  // Convert "test %reg, foo@GOTPCREL(%rip)" to "test $foo, %reg".
  if (Op == 0x85) {
    // See "TEST-Logical Compare" (4-428 Vol. 2B),
    // TEST r/m64, r64 uses "full" ModR / M byte (no opcode extension).

    // ModR/M byte has form XX YYY ZZZ, where
    // YYY is MODRM.reg(register 2), ZZZ is MODRM.rm(register 1).
    // XX has different meanings:
    // 00: The operand's memory address is in reg1.
    // 01: The operand's memory address is reg1 + a byte-sized displacement.
    // 10: The operand's memory address is reg1 + a word-sized displacement.
    // 11: The operand is reg1 itself.
    // If an instruction requires only one operand, the unused reg2 field
    // holds extra opcode bits rather than a register code
    // 0xC0 == 11 000 000 binary.
    // 0x38 == 00 111 000 binary.
    // We transfer reg2 to reg1 here as operand.
    // See "2.1.3 ModR/M and SIB Bytes" (Vol. 2A 2-3).
    Loc[-1] = 0xc0 | (ModRm & 0x38) >> 3; // ModR/M byte.

    // Change opcode from TEST r/m64, r64 to TEST r/m64, imm32
    // See "TEST-Logical Compare" (4-428 Vol. 2B).
    Loc[-2] = 0xf7;

    // Move R bit to the B bit in REX byte.
    // REX byte is encoded as 0100WRXB, where
    // 0100 is 4bit fixed pattern.
    // REX.W When 1, a 64-bit operand size is used. Otherwise, when 0, the
    //   default operand size is used (which is 32-bit for most but not all
    //   instructions).
    // REX.R This 1-bit value is an extension to the MODRM.reg field.
    // REX.X This 1-bit value is an extension to the SIB.index field.
    // REX.B This 1-bit value is an extension to the MODRM.rm field or the
    // SIB.base field.
    // See "2.2.1.2 More on REX Prefix Fields " (2-8 Vol. 2A).
    Loc[-3] = (Rex & ~0x4) | (Rex & 0x4) >> 2;
    write32le(Loc, Val);
    return;
  }

  // If we are here then we need to relax the adc, add, and, cmp, or, sbb, sub
  // or xor operations.

  // Convert "binop foo@GOTPCREL(%rip), %reg" to "binop $foo, %reg".
  // Logic is close to one for test instruction above, but we also
  // write opcode extension here, see below for details.
  Loc[-1] = 0xc0 | (ModRm & 0x38) >> 3 | (Op & 0x3c); // ModR/M byte.

  // Primary opcode is 0x81, opcode extension is one of:
  // 000b = ADD, 001b is OR, 010b is ADC, 011b is SBB,
  // 100b is AND, 101b is SUB, 110b is XOR, 111b is CMP.
  // This value was wrote to MODRM.reg in a line above.
  // See "3.2 INSTRUCTIONS (A-M)" (Vol. 2A 3-15),
  // "INSTRUCTION SET REFERENCE, N-Z" (Vol. 2B 4-1) for
  // descriptions about each operation.
  Loc[-2] = 0x81;
  Loc[-3] = (Rex & ~0x4) | (Rex & 0x4) >> 2;
  write32le(Loc, Val);
}

void X86_64::relaxGot(uint8_t *Loc, RelType Type, uint64_t Val) const {
  const uint8_t Op = Loc[-2];
  const uint8_t ModRm = Loc[-1];

  // Convert "mov foo@GOTPCREL(%rip),%reg" to "lea foo(%rip),%reg".
  if (Op == 0x8b) {
    Loc[-2] = 0x8d;
    write32le(Loc, Val);
    return;
  }

  if (Op != 0xff) {
    // We are relaxing a rip relative to an absolute, so compensate
    // for the old -4 addend.
    assert(!Config->Pic);
    relaxGotNoPic(Loc, Val + 4, Op, ModRm);
    return;
  }

  // Convert call/jmp instructions.
  if (ModRm == 0x15) {
    // ABI says we can convert "call *foo@GOTPCREL(%rip)" to "nop; call foo".
    // Instead we convert to "addr32 call foo" where addr32 is an instruction
    // prefix. That makes result expression to be a single instruction.
    Loc[-2] = 0x67; // addr32 prefix
    Loc[-1] = 0xe8; // call
    write32le(Loc, Val);
    return;
  }

  // Convert "jmp *foo@GOTPCREL(%rip)" to "jmp foo; nop".
  // jmp doesn't return, so it is fine to use nop here, it is just a stub.
  assert(ModRm == 0x25);
  Loc[-2] = 0xe9; // jmp
  Loc[3] = 0x90;  // nop
  write32le(Loc - 1, Val + 1);
}

// A split-stack prologue starts by checking the amount of stack remaining
// in one of two ways:
// A) Comparing of the stack pointer to a field in the tcb.
// B) Or a load of a stack pointer offset with an lea to r10 or r11.
bool X86_64::adjustPrologueForCrossSplitStack(uint8_t *Loc, uint8_t *End,
                                              uint8_t StOther) const {
  if (!Config->Is64) {
    error("Target doesn't support split stacks.");
    return false;
  }

  if (Loc + 8 >= End)
    return false;

  // Replace "cmp %fs:0x70,%rsp" and subsequent branch
  // with "stc, nopl 0x0(%rax,%rax,1)"
  if (memcmp(Loc, "\x64\x48\x3b\x24\x25", 5) == 0) {
    memcpy(Loc, "\xf9\x0f\x1f\x84\x00\x00\x00\x00", 8);
    return true;
  }

  // Adjust "lea X(%rsp),%rYY" to lea "(X - 0x4000)(%rsp),%rYY" where rYY could
  // be r10 or r11. The lea instruction feeds a subsequent compare which checks
  // if there is X available stack space. Making X larger effectively reserves
  // that much additional space. The stack grows downward so subtract the value.
  if (memcmp(Loc, "\x4c\x8d\x94\x24", 4) == 0 ||
      memcmp(Loc, "\x4c\x8d\x9c\x24", 4) == 0) {
    // The offset bytes are encoded four bytes after the start of the
    // instruction.
    write32le(Loc + 4, read32le(Loc + 4) - 0x4000);
    return true;
  }
  return false;
}

// These nonstandard PLT entries are to migtigate Spectre v2 security
// vulnerability. In order to mitigate Spectre v2, we want to avoid indirect
// branch instructions such as `jmp *GOTPLT(%rip)`. So, in the following PLT
// entries, we use a CALL followed by MOV and RET to do the same thing as an
// indirect jump. That instruction sequence is so-called "retpoline".
//
// We have two types of retpoline PLTs as a size optimization. If `-z now`
// is specified, all dynamic symbols are resolved at load-time. Thus, when
// that option is given, we can omit code for symbol lazy resolution.
namespace {
class Retpoline : public X86_64 {
public:
  Retpoline();
  void writeGotPlt(uint8_t *Buf, const Symbol &S) const override;
  void writePltHeader(uint8_t *Buf) const override;
  void writePlt(uint8_t *Buf, uint64_t GotPltEntryAddr, uint64_t PltEntryAddr,
                int32_t Index, unsigned RelOff) const override;
};

class RetpolineZNow : public X86_64 {
public:
  RetpolineZNow();
  void writeGotPlt(uint8_t *Buf, const Symbol &S) const override {}
  void writePltHeader(uint8_t *Buf) const override;
  void writePlt(uint8_t *Buf, uint64_t GotPltEntryAddr, uint64_t PltEntryAddr,
                int32_t Index, unsigned RelOff) const override;
};
} // namespace

Retpoline::Retpoline() {
  PltHeaderSize = 48;
  PltEntrySize = 32;
}

void Retpoline::writeGotPlt(uint8_t *Buf, const Symbol &S) const {
  write64le(Buf, S.getPltVA() + 17);
}

void Retpoline::writePltHeader(uint8_t *Buf) const {
  const uint8_t Insn[] = {
      0xff, 0x35, 0,    0,    0,    0,          // 0:    pushq GOTPLT+8(%rip)
      0x4c, 0x8b, 0x1d, 0,    0,    0,    0,    // 6:    mov GOTPLT+16(%rip), %r11
      0xe8, 0x0e, 0x00, 0x00, 0x00,             // d:    callq next
      0xf3, 0x90,                               // 12: loop: pause
      0x0f, 0xae, 0xe8,                         // 14:   lfence
      0xeb, 0xf9,                               // 17:   jmp loop
      0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, // 19:   int3; .align 16
      0x4c, 0x89, 0x1c, 0x24,                   // 20: next: mov %r11, (%rsp)
      0xc3,                                     // 24:   ret
      0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, // 25:   int3; padding
      0xcc, 0xcc, 0xcc, 0xcc,                   // 2c:   int3; padding
  };
  memcpy(Buf, Insn, sizeof(Insn));

  uint64_t GotPlt = In.GotPlt->getVA();
  uint64_t Plt = In.Plt->getVA();
  write32le(Buf + 2, GotPlt - Plt - 6 + 8);
  write32le(Buf + 9, GotPlt - Plt - 13 + 16);
}

void Retpoline::writePlt(uint8_t *Buf, uint64_t GotPltEntryAddr,
                         uint64_t PltEntryAddr, int32_t Index,
                         unsigned RelOff) const {
  const uint8_t Insn[] = {
      0x4c, 0x8b, 0x1d, 0, 0, 0, 0, // 0:  mov foo@GOTPLT(%rip), %r11
      0xe8, 0,    0,    0,    0,    // 7:  callq plt+0x20
      0xe9, 0,    0,    0,    0,    // c:  jmp plt+0x12
      0x68, 0,    0,    0,    0,    // 11: pushq <relocation index>
      0xe9, 0,    0,    0,    0,    // 16: jmp plt+0
      0xcc, 0xcc, 0xcc, 0xcc, 0xcc, // 1b: int3; padding
  };
  memcpy(Buf, Insn, sizeof(Insn));

  uint64_t Off = PltHeaderSize + PltEntrySize * Index;

  write32le(Buf + 3, GotPltEntryAddr - PltEntryAddr - 7);
  write32le(Buf + 8, -Off - 12 + 32);
  write32le(Buf + 13, -Off - 17 + 18);
  write32le(Buf + 18, Index);
  write32le(Buf + 23, -Off - 27);
}

RetpolineZNow::RetpolineZNow() {
  PltHeaderSize = 32;
  PltEntrySize = 16;
}

void RetpolineZNow::writePltHeader(uint8_t *Buf) const {
  const uint8_t Insn[] = {
      0xe8, 0x0b, 0x00, 0x00, 0x00, // 0:    call next
      0xf3, 0x90,                   // 5:  loop: pause
      0x0f, 0xae, 0xe8,             // 7:    lfence
      0xeb, 0xf9,                   // a:    jmp loop
      0xcc, 0xcc, 0xcc, 0xcc,       // c:    int3; .align 16
      0x4c, 0x89, 0x1c, 0x24,       // 10: next: mov %r11, (%rsp)
      0xc3,                         // 14:   ret
      0xcc, 0xcc, 0xcc, 0xcc, 0xcc, // 15:   int3; padding
      0xcc, 0xcc, 0xcc, 0xcc, 0xcc, // 1a:   int3; padding
      0xcc,                         // 1f:   int3; padding
  };
  memcpy(Buf, Insn, sizeof(Insn));
}

void RetpolineZNow::writePlt(uint8_t *Buf, uint64_t GotPltEntryAddr,
                             uint64_t PltEntryAddr, int32_t Index,
                             unsigned RelOff) const {
  const uint8_t Insn[] = {
      0x4c, 0x8b, 0x1d, 0,    0, 0, 0, // mov foo@GOTPLT(%rip), %r11
      0xe9, 0,    0,    0,    0,       // jmp plt+0
      0xcc, 0xcc, 0xcc, 0xcc,          // int3; padding
  };
  memcpy(Buf, Insn, sizeof(Insn));

  write32le(Buf + 3, GotPltEntryAddr - PltEntryAddr - 7);
  write32le(Buf + 8, -PltHeaderSize - PltEntrySize * Index - 12);
}

static TargetInfo *getTargetInfo() {
  if (Config->ZRetpolineplt) {
    if (Config->ZNow) {
      static RetpolineZNow T;
      return &T;
    }
    static Retpoline T;
    return &T;
  }

  static X86_64 T;
  return &T;
}

TargetInfo *elf::getX86_64TargetInfo() { return getTargetInfo(); }
