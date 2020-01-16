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

namespace lld {
namespace elf {

namespace {
class X86_64 : public TargetInfo {
public:
  X86_64();
  int getTlsGdRelaxSkip(RelType type) const override;
  RelExpr getRelExpr(RelType type, const Symbol &s,
                     const uint8_t *loc) const override;
  RelType getDynRel(RelType type) const override;
  void writeGotPltHeader(uint8_t *buf) const override;
  void writeGotPlt(uint8_t *buf, const Symbol &s) const override;
  void writePltHeader(uint8_t *buf) const override;
  void writePlt(uint8_t *buf, const Symbol &sym,
                uint64_t pltEntryAddr) const override;
  void relocateOne(uint8_t *loc, RelType type, uint64_t val) const override;
  void relocateOneJumpRelocation(uint8_t *Loc, JumpRelType Type,
                                 unsigned Size) const override;

  RelExpr adjustRelaxExpr(RelType type, const uint8_t *data,
                          RelExpr expr) const override;
  void relaxGot(uint8_t *loc, RelType type, uint64_t val) const override;
  void relaxTlsGdToIe(uint8_t *loc, RelType type, uint64_t val) const override;
  void relaxTlsGdToLe(uint8_t *loc, RelType type, uint64_t val) const override;
  void relaxTlsIeToLe(uint8_t *loc, RelType type, uint64_t val) const override;
  void relaxTlsLdToLe(uint8_t *loc, RelType type, uint64_t val) const override;
  bool adjustPrologueForCrossSplitStack(uint8_t *loc, uint8_t *end,
                                        uint8_t stOther) const override;
  bool deleteFallThruJmpInsn(InputSection &IS, InputFile *File,
                             InputSection *NextIS) const override;
  unsigned shrinkJmpInsn(InputSection &IS, InputFile *File,
                         uint32_t MaxAlignment) const override;
  unsigned growJmpInsn(InputSection &IS, InputFile *File,
                       uint32_t MaxAlignment) const override;
};
} // namespace

static std::vector<std::vector<uint8_t>> X86_NOP_INSTRUCTIONS = {
    {0x90},
    {0x66, 0x90},
    {0x0f, 0x1f, 0x00},
    {0x0f, 0x1f, 0x40, 0x00},
    {0x0f, 0x1f, 0x44, 0x00, 0x00},
    {0x66, 0x0f, 0x1f, 0x44, 0x00, 0x00},
    {0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00},
    {0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x66, 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00}};

X86_64::X86_64() {
  copyRel = R_X86_64_COPY;
  gotRel = R_X86_64_GLOB_DAT;
  noneRel = R_X86_64_NONE;
  pltRel = R_X86_64_JUMP_SLOT;
  relativeRel = R_X86_64_RELATIVE;
  iRelativeRel = R_X86_64_IRELATIVE;
  symbolicRel = R_X86_64_64;
  tlsDescRel = R_X86_64_TLSDESC;
  tlsGotRel = R_X86_64_TPOFF64;
  tlsModuleIndexRel = R_X86_64_DTPMOD64;
  tlsOffsetRel = R_X86_64_DTPOFF64;
  pltHeaderSize = 16;
  pltEntrySize = 16;
  ipltEntrySize = 16;
  trapInstr = {0xcc, 0xcc, 0xcc, 0xcc}; // 0xcc = INT3

  // Align to the large page size (known as a superpage or huge page).
  // FreeBSD automatically promotes large, superpage-aligned allocations.
  defaultImageBase = 0x200000;
}

int X86_64::getTlsGdRelaxSkip(RelType type) const { return 2; }

// Opcodes for the different X86_64 jmp instructions.
enum JmpInsnOpcode : uint32_t {
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
  if (*Second == 0xe9)
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

// Return the relocation index for input section IS with a specific Offset.
// Returns the maximum size of the vector if no such relocation is found.
static unsigned getRelocationWithOffset(const InputSection &IS,
                                        uint64_t Offset) {
  unsigned I = 0;
  for (; I < IS.relocations.size(); ++I) {
    if (IS.relocations[I].offset == Offset && IS.relocations[I].expr != R_NONE)
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
  return (R.type == R_X86_64_PLT32 || R.type == R_X86_64_PC32 ||
          R.type == R_X86_64_PC8);
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

  uint64_t AddrLoc = (IS.getOutputSection())->addr + IS.outSecOff + R.offset;
  uint64_t TargetOffset =
      SignExtend64(InputSectionBase::getRelocTargetVA(File, R.type, R.addend,
                                                      AddrLoc, *R.sym, R.expr),
                   (config->wordsize * 8));

  // If this jmp is a fall thru, the target offset is the beginning of the
  // next section.
  uint64_t NextSectionOffset =
      NextIS->getOutputSection()->addr + NextIS->outSecOff;
  if ((AddrLoc + 4 + TargetOffset) != NextSectionOffset)
    return false;

  return true;
}

// Return the jmp instruction opcode that is the inverse of the given
// opcode.  For example, JE inverted is JNE.
static JmpInsnOpcode invertJmpOpcode(const JmpInsnOpcode opcode) {
  switch (opcode) {
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

// Deletes direct jump instruction in input sections that jumps to the
// following section as it is not required.  If there are two consecutive jump
// instructions, it checks if they can be flipped and one can be deleted.
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
  if (RIndex == IS.relocations.size())
    return false;

  Relocation &R = IS.relocations[RIndex];

  // Check if the relocation corresponds to a direct jmp.
  const uint8_t *SecContents = IS.data().data();
  if (!isDirectJmpInsnOpcode(SecContents + R.offset - 1))
    return false;

  if (isFallThruRelocation(IS, File, NextIS, R)) {
    // This is a fall thru and can be deleted.
    R.expr = R_NONE;
    R.offset = 0;
    IS.drop_back(SizeOfDirectJmpInsn);
    IS.SpecialFiller = X86_NOP_INSTRUCTIONS;
    return true;
  }

  // Now, check if flip and delete is possible.
  const unsigned SizeOfJmpCCInsn = 6;
  // To flip, there must be atleast one JmpCC and one direct jmp.
  if (IS.getSize() < (SizeOfDirectJmpInsn + SizeOfJmpCCInsn))
    return 0;

  unsigned RbIndex =
      getRelocationWithOffset(IS, (IS.getSize() - SizeOfDirectJmpInsn - 4));
  if (RbIndex == IS.relocations.size())
    return 0;

  Relocation &Rb = IS.relocations[RbIndex];

  const uint8_t *JmpInsnB = SecContents + Rb.offset - 1;
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
  IS.addJumpRelocation({JInvert, (Rb.offset - 1), 4});
  // Move R's values to Rb
  Rb.expr = R.expr;
  Rb.type = R.type;
  Rb.addend = R.addend;
  Rb.sym = R.sym;
  // Cancel R
  R.expr = R_NONE;
  R.offset = 0;
  IS.drop_back(SizeOfDirectJmpInsn);
  IS.SpecialFiller = X86_NOP_INSTRUCTIONS;
  return true;
}

// Returns target offset if the Relocation R corresponds to a jmp instruction
// and the offset of the relocation is 1 byte wide.
static uint64_t getTargetOffsetForJmp(InputSection &IS, InputFile *File,
                                      Relocation &R, JmpInsnOpcode &JmpCode) {
  const unsigned SizeOfJmpCCOpcode = 2;

  if (!isRelocationForJmpInsn(R)) {
    return false;
  }

  unsigned JIndex = getJumpRelocationWithOffset(IS, (R.offset - 1));
  if (JIndex != IS.JumpRelocations.size()) {
    JmpCode = static_cast<JmpInsnOpcode>(IS.JumpRelocations[JIndex].Original);
  } else {
    const uint8_t *SecContents = IS.data().data();
    const uint8_t *JmpInsn = SecContents + R.offset - 1;
    const uint8_t *JmpCCInsn =
        (R.offset >= SizeOfJmpCCOpcode) ? (JmpInsn - 1) : nullptr;
    JmpCode = getJmpInsnType(JmpCCInsn, JmpInsn);
  }
  if (JmpCode == J_UNKNOWN) {
    return 0;
  }

  uint64_t AddrLoc = (IS.getOutputSection())->addr + IS.outSecOff + R.offset;
  uint64_t TargetOffset =
      SignExtend64(InputSectionBase::getRelocTargetVA(File, R.type, R.addend,
                                                      AddrLoc, *R.sym, R.expr),
                   (config->wordsize * 8));

  return TargetOffset;
}

static bool isOneByteOffsetWhenShrunk(uint64_t TargetOffset,
                                      JmpInsnOpcode JmpCode,
                                      unsigned BytesShrunk, unsigned MaxAlign) {
  return true;
  // For negative jumps, the jump target will be closer if shrinking
  // is done.
  if ((int64_t)TargetOffset < 0) {
    TargetOffset += BytesShrunk;
    TargetOffset += (JmpCode == J_JMP_32) ? 3 : 4;
  }

  if (MaxAlign > 0) {
    if ((int64_t)TargetOffset < 0) {
      TargetOffset -= (MaxAlign - 1);
    } else {
      TargetOffset += (MaxAlign - 1);
    }
  }

  return ((int64_t)TargetOffset == llvm::SignExtend64(TargetOffset, 8));
}

static bool isOneByteOffset(uint64_t TargetOffset, unsigned BytesGrown,
                            unsigned MaxAlign) {
  // For negative jumps, the jump target is further.
  if ((int64_t)TargetOffset < 0) {
    TargetOffset -= BytesGrown;
  }
  return ((int64_t)TargetOffset == llvm::SignExtend64(TargetOffset, 8));
}

static void shrinkJmpWithRelocation(InputSection &IS, JmpInsnOpcode JmpCode,
                                    Relocation &R, unsigned &BytesShrunk,
                                    bool DoShrinkJmp = true) {
  // Check if there is a Jump Relocation against this offset.
  unsigned JIndex = getJumpRelocationWithOffset(IS, (R.offset - 1));

  if (DoShrinkJmp && JmpCode != J_JMP_32)
    BytesShrunk += 1;

  // Update R.offset
  R.offset -= BytesShrunk;
  unsigned NewJmpSize = DoShrinkJmp ? 1 : 4;

  if (JIndex < IS.JumpRelocations.size()) {
    JumpRelocation &J = IS.JumpRelocations[JIndex];
    assert((!DoShrinkJmp || J.Size == 4) && "Not the right size of jump.");
    J.Offset = R.offset - 1;
    if (DoShrinkJmp)
      J.Size = NewJmpSize;
  } else {
    IS.addJumpRelocation({JmpCode, R.offset - 1, NewJmpSize});
  }

  if (DoShrinkJmp) {
    // Shrinking Jmp corresponding to relocation R, adjust type and addend.
    R.type = R_X86_64_PC8;
    R.addend += 3;
    BytesShrunk += 3;
  }
}

unsigned X86_64::shrinkJmpInsn(InputSection &IS, InputFile *File,
                               unsigned MaxAlign) const {
  const unsigned SizeOfDirectShortJmpInsn = 2;
  const unsigned SizeOfDirectNearJmpInsn = 5;
  const unsigned SizeOfJmpCCInsn = 6;
  int SizeOfDirectJmpInsn = SizeOfDirectNearJmpInsn;

  bool IsShortJmp = false;

  if (IS.getSize() < SizeOfDirectNearJmpInsn)
    return 0;

  unsigned RIndex = getRelocationWithOffset(IS, (IS.getSize() - 4));

  if (RIndex == IS.relocations.size()) {
    RIndex = getRelocationWithOffset(IS, (IS.getSize() - 1));
    if (RIndex == IS.relocations.size())
      return 0;

    SizeOfDirectJmpInsn = SizeOfDirectShortJmpInsn;
    IsShortJmp = true;
  }

  Relocation &R = IS.relocations[RIndex];

  JmpInsnOpcode JmpCode = J_UNKNOWN;

  uint64_t TargetOffset = getTargetOffsetForJmp(IS, File, R, JmpCode);
  bool DirectJmp = (JmpCode == J_JMP_32);

  if (JmpCode == J_UNKNOWN) {
    return 0;
  }

  unsigned BytesShrunk = 0;

  if (!DirectJmp) {
    if (!IsShortJmp && isOneByteOffsetWhenShrunk(TargetOffset, JmpCode,
                                                 BytesShrunk, MaxAlign)) {
      shrinkJmpWithRelocation(IS, JmpCode, R, BytesShrunk);
    }
  } else {
    // For Direct Jmps, the previous insn might be a jmpcc that can be
    // shrinked.  Check that also.
    if (IS.getSize() >= (SizeOfDirectJmpInsn + SizeOfJmpCCInsn)) {
      unsigned RbIndex =
          getRelocationWithOffset(IS, (IS.getSize() - SizeOfDirectJmpInsn - 4));

      if (RbIndex != IS.relocations.size()) {
        Relocation &Rb = IS.relocations[RbIndex];
        JmpInsnOpcode JmpCode_B = J_UNKNOWN;
        uint64_t TargetOffset_B =
            getTargetOffsetForJmp(IS, File, Rb, JmpCode_B);
        if (JmpCode_B != J_UNKNOWN && JmpCode_B != J_JMP_32 &&
            isOneByteOffsetWhenShrunk(TargetOffset_B, JmpCode, BytesShrunk,
                                      MaxAlign)) {
          shrinkJmpWithRelocation(IS, JmpCode_B, Rb, BytesShrunk);
        }
      }
    }
    bool CanShrinkR =
        !IsShortJmp &&
        isOneByteOffsetWhenShrunk(TargetOffset, JmpCode, BytesShrunk, MaxAlign);
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
  unsigned JIndex = getJumpRelocationWithOffset(IS, (R.offset - 1));

  if (JIndex == IS.JumpRelocations.size()) {
    error("Jump relocation does not exist!");
    return;
  }

  if (DoGrowJmp && JmpCode != J_JMP_32)
    BytesGrown += 1;

  // Update R.offset
  R.offset += BytesGrown;

  JumpRelocation &J = IS.JumpRelocations[JIndex];
  assert((!DoGrowJmp || J.Size == 1) && "Not the right size of jump.");
  J.Offset = R.offset - 1;
  if (DoGrowJmp) {
    // Growing Jmp corresponding to relocation R, adjust type and addend.
    J.Size = 4;
    R.type = R_X86_64_PC32;
    // assert(R.addend == -1 && "Addend must be -1 to grow.");
    R.addend -= 3;
    BytesGrown += 3;
  }
}

unsigned X86_64::growJmpInsn(InputSection &IS, InputFile *File,
                             unsigned MaxAlign) const {
  const unsigned SizeOfJmpCCInsn = 2;
  const unsigned SizeOfDirectNearJmpInsn = 5;
  const unsigned SizeOfDirectShortJmpInsn = 2;
  int SizeOfDirectJmpInsn = SizeOfDirectShortJmpInsn;

  if (IS.getSize() < SizeOfDirectShortJmpInsn)
    return 0;

  bool IsShortJmp = true;

  unsigned RIndex = getRelocationWithOffset(IS, IS.getSize() - 1);

  if (RIndex == IS.relocations.size()) {
    if (IS.getSize() < SizeOfDirectNearJmpInsn)
      return 0;

    RIndex = getRelocationWithOffset(IS, (IS.getSize() - 4));
    if (RIndex == IS.relocations.size())
      return 0;

    IsShortJmp = false;
    SizeOfDirectJmpInsn = SizeOfDirectNearJmpInsn;
  }

  Relocation &R = IS.relocations[RIndex];

  JmpInsnOpcode JmpCode = J_UNKNOWN;

  uint64_t TargetOffset = getTargetOffsetForJmp(IS, File, R, JmpCode);
  bool DirectJmp = (JmpCode == J_JMP_32);

  if (JmpCode == J_UNKNOWN) {
    return 0;
  }

  unsigned BytesGrown = 0;
  if (!DirectJmp) {
    // Grow JmpInsn.
    if (IsShortJmp && !isOneByteOffset(TargetOffset, BytesGrown, MaxAlign)) {
      growJmpWithRelocation(IS, JmpCode, R, BytesGrown);
    }
  } else {
    // For Direct Jmps, the previous insn might be a jmpcc that might need
    // to be grown.  Check that also.
    if (IS.getSize() >= (SizeOfDirectJmpInsn + SizeOfJmpCCInsn)) {
      unsigned RbIndex =
          getRelocationWithOffset(IS, (IS.getSize() - SizeOfDirectJmpInsn - 1));

      if (RbIndex != IS.relocations.size()) {
        Relocation &Rb = IS.relocations[RbIndex];
        JmpInsnOpcode JmpCode_B = J_UNKNOWN;
        uint64_t TargetOffset_B =
            getTargetOffsetForJmp(IS, File, Rb, JmpCode_B);
        if (JmpCode_B != J_UNKNOWN && JmpCode_B != J_JMP_32 &&
            !isOneByteOffset(TargetOffset_B, BytesGrown, MaxAlign)) {
          growJmpWithRelocation(IS, JmpCode_B, Rb, BytesGrown);
        }
      }
    }
    bool ShouldGrowR =
        IsShortJmp && !isOneByteOffset(TargetOffset, BytesGrown, MaxAlign);
    growJmpWithRelocation(IS, JmpCode, R, BytesGrown, ShouldGrowR);
  }

  if (BytesGrown)
    IS.push_back(BytesGrown);

  return BytesGrown;
}

RelExpr X86_64::getRelExpr(RelType type, const Symbol &s,
                           const uint8_t *loc) const {
  if (type == R_X86_64_GOTTPOFF)
    config->hasStaticTlsModel = true;

  switch (type) {
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
    error(getErrorLocation(loc) + "unknown relocation (" + Twine(type) +
          ") against symbol " + toString(s));
    return R_NONE;
  }
}

void X86_64::writeGotPltHeader(uint8_t *buf) const {
  // The first entry holds the value of _DYNAMIC. It is not clear why that is
  // required, but it is documented in the psabi and the glibc dynamic linker
  // seems to use it (note that this is relevant for linking ld.so, not any
  // other program).
  write64le(buf, mainPart->dynamic->getVA());
}

void X86_64::writeGotPlt(uint8_t *buf, const Symbol &s) const {
  // See comments in X86::writeGotPlt.
  write64le(buf, s.getPltVA() + 6);
}

void X86_64::writePltHeader(uint8_t *buf) const {
  const uint8_t pltData[] = {
      0xff, 0x35, 0, 0, 0, 0, // pushq GOTPLT+8(%rip)
      0xff, 0x25, 0, 0, 0, 0, // jmp *GOTPLT+16(%rip)
      0x0f, 0x1f, 0x40, 0x00, // nop
  };
  memcpy(buf, pltData, sizeof(pltData));
  uint64_t gotPlt = in.gotPlt->getVA();
  uint64_t plt = in.ibtPlt ? in.ibtPlt->getVA() : in.plt->getVA();
  write32le(buf + 2, gotPlt - plt + 2); // GOTPLT+8
  write32le(buf + 8, gotPlt - plt + 4); // GOTPLT+16
}

void X86_64::writePlt(uint8_t *buf, const Symbol &sym,
                      uint64_t pltEntryAddr) const {
  const uint8_t inst[] = {
      0xff, 0x25, 0, 0, 0, 0, // jmpq *got(%rip)
      0x68, 0, 0, 0, 0,       // pushq <relocation index>
      0xe9, 0, 0, 0, 0,       // jmpq plt[0]
  };
  memcpy(buf, inst, sizeof(inst));

  write32le(buf + 2, sym.getGotPltVA() - pltEntryAddr - 6);
  write32le(buf + 7, sym.pltIndex);
  write32le(buf + 12, in.plt->getVA() - pltEntryAddr - 16);
}

RelType X86_64::getDynRel(RelType type) const {
  if (type == R_X86_64_64 || type == R_X86_64_PC64 || type == R_X86_64_SIZE32 ||
      type == R_X86_64_SIZE64)
    return type;
  return R_X86_64_NONE;
}

void X86_64::relaxTlsGdToLe(uint8_t *loc, RelType type, uint64_t val) const {
  if (type == R_X86_64_TLSGD) {
    // Convert
    //   .byte 0x66
    //   leaq x@tlsgd(%rip), %rdi
    //   .word 0x6666
    //   rex64
    //   call __tls_get_addr@plt
    // to the following two instructions.
    const uint8_t inst[] = {
        0x64, 0x48, 0x8b, 0x04, 0x25, 0x00, 0x00,
        0x00, 0x00,                            // mov %fs:0x0,%rax
        0x48, 0x8d, 0x80, 0,    0,    0,    0, // lea x@tpoff,%rax
    };
    memcpy(loc - 4, inst, sizeof(inst));

    // The original code used a pc relative relocation and so we have to
    // compensate for the -4 in had in the addend.
    write32le(loc + 8, val + 4);
  } else {
    // Convert
    //   lea x@tlsgd(%rip), %rax
    //   call *(%rax)
    // to the following two instructions.
    assert(type == R_X86_64_GOTPC32_TLSDESC);
    if (memcmp(loc - 3, "\x48\x8d\x05", 3)) {
      error(getErrorLocation(loc - 3) + "R_X86_64_GOTPC32_TLSDESC must be used "
                                        "in callq *x@tlsdesc(%rip), %rax");
      return;
    }
    // movq $x@tpoff(%rip),%rax
    loc[-2] = 0xc7;
    loc[-1] = 0xc0;
    write32le(loc, val + 4);
    // xchg ax,ax
    loc[4] = 0x66;
    loc[5] = 0x90;
  }
}

void X86_64::relaxTlsGdToIe(uint8_t *loc, RelType type, uint64_t val) const {
  if (type == R_X86_64_TLSGD) {
    // Convert
    //   .byte 0x66
    //   leaq x@tlsgd(%rip), %rdi
    //   .word 0x6666
    //   rex64
    //   call __tls_get_addr@plt
    // to the following two instructions.
    const uint8_t inst[] = {
        0x64, 0x48, 0x8b, 0x04, 0x25, 0x00, 0x00,
        0x00, 0x00,                            // mov %fs:0x0,%rax
        0x48, 0x03, 0x05, 0,    0,    0,    0, // addq x@gottpoff(%rip),%rax
    };
    memcpy(loc - 4, inst, sizeof(inst));

    // Both code sequences are PC relatives, but since we are moving the
    // constant forward by 8 bytes we have to subtract the value by 8.
    write32le(loc + 8, val - 8);
  } else {
    // Convert
    //   lea x@tlsgd(%rip), %rax
    //   call *(%rax)
    // to the following two instructions.
    assert(type == R_X86_64_GOTPC32_TLSDESC);
    if (memcmp(loc - 3, "\x48\x8d\x05", 3)) {
      error(getErrorLocation(loc - 3) + "R_X86_64_GOTPC32_TLSDESC must be used "
                                        "in callq *x@tlsdesc(%rip), %rax");
      return;
    }
    // movq x@gottpoff(%rip),%rax
    loc[-2] = 0x8b;
    write32le(loc, val);
    // xchg ax,ax
    loc[4] = 0x66;
    loc[5] = 0x90;
  }
}

// In some conditions, R_X86_64_GOTTPOFF relocation can be optimized to
// R_X86_64_TPOFF32 so that it does not use GOT.
void X86_64::relaxTlsIeToLe(uint8_t *loc, RelType type, uint64_t val) const {
  uint8_t *inst = loc - 3;
  uint8_t reg = loc[-1] >> 3;
  uint8_t *regSlot = loc - 1;

  // Note that ADD with RSP or R12 is converted to ADD instead of LEA
  // because LEA with these registers needs 4 bytes to encode and thus
  // wouldn't fit the space.

  if (memcmp(inst, "\x48\x03\x25", 3) == 0) {
    // "addq foo@gottpoff(%rip),%rsp" -> "addq $foo,%rsp"
    memcpy(inst, "\x48\x81\xc4", 3);
  } else if (memcmp(inst, "\x4c\x03\x25", 3) == 0) {
    // "addq foo@gottpoff(%rip),%r12" -> "addq $foo,%r12"
    memcpy(inst, "\x49\x81\xc4", 3);
  } else if (memcmp(inst, "\x4c\x03", 2) == 0) {
    // "addq foo@gottpoff(%rip),%r[8-15]" -> "leaq foo(%r[8-15]),%r[8-15]"
    memcpy(inst, "\x4d\x8d", 2);
    *regSlot = 0x80 | (reg << 3) | reg;
  } else if (memcmp(inst, "\x48\x03", 2) == 0) {
    // "addq foo@gottpoff(%rip),%reg -> "leaq foo(%reg),%reg"
    memcpy(inst, "\x48\x8d", 2);
    *regSlot = 0x80 | (reg << 3) | reg;
  } else if (memcmp(inst, "\x4c\x8b", 2) == 0) {
    // "movq foo@gottpoff(%rip),%r[8-15]" -> "movq $foo,%r[8-15]"
    memcpy(inst, "\x49\xc7", 2);
    *regSlot = 0xc0 | reg;
  } else if (memcmp(inst, "\x48\x8b", 2) == 0) {
    // "movq foo@gottpoff(%rip),%reg" -> "movq $foo,%reg"
    memcpy(inst, "\x48\xc7", 2);
    *regSlot = 0xc0 | reg;
  } else {
    error(getErrorLocation(loc - 3) +
          "R_X86_64_GOTTPOFF must be used in MOVQ or ADDQ instructions only");
  }

  // The original code used a PC relative relocation.
  // Need to compensate for the -4 it had in the addend.
  write32le(loc, val + 4);
}

void X86_64::relaxTlsLdToLe(uint8_t *loc, RelType type, uint64_t val) const {
  if (type == R_X86_64_DTPOFF64) {
    write64le(loc, val);
    return;
  }
  if (type == R_X86_64_DTPOFF32) {
    write32le(loc, val);
    return;
  }

  const uint8_t inst[] = {
      0x66, 0x66,                                           // .word 0x6666
      0x66,                                                 // .byte 0x66
      0x64, 0x48, 0x8b, 0x04, 0x25, 0x00, 0x00, 0x00, 0x00, // mov %fs:0,%rax
  };

  if (loc[4] == 0xe8) {
    // Convert
    //   leaq bar@tlsld(%rip), %rdi           # 48 8d 3d <Loc>
    //   callq __tls_get_addr@PLT             # e8 <disp32>
    //   leaq bar@dtpoff(%rax), %rcx
    // to
    //   .word 0x6666
    //   .byte 0x66
    //   mov %fs:0,%rax
    //   leaq bar@tpoff(%rax), %rcx
    memcpy(loc - 3, inst, sizeof(inst));
    return;
  }

  if (loc[4] == 0xff && loc[5] == 0x15) {
    // Convert
    //   leaq  x@tlsld(%rip),%rdi               # 48 8d 3d <Loc>
    //   call *__tls_get_addr@GOTPCREL(%rip)    # ff 15 <disp32>
    // to
    //   .long  0x66666666
    //   movq   %fs:0,%rax
    // See "Table 11.9: LD -> LE Code Transition (LP64)" in
    // https://raw.githubusercontent.com/wiki/hjl-tools/x86-psABI/x86-64-psABI-1.0.pdf
    loc[-3] = 0x66;
    memcpy(loc - 2, inst, sizeof(inst));
    return;
  }

  error(getErrorLocation(loc - 3) +
        "expected R_X86_64_PLT32 or R_X86_64_GOTPCRELX after R_X86_64_TLSLD");
}

void X86_64::relocateOneJumpRelocation(uint8_t *Loc, JumpRelType Type,
                                       unsigned Size) const {
  switch (Type) {
  case J_JMP_32:
    if (Size == 4)
      *Loc = 0xe9;
    else
      *Loc = 0xeb;
    break;
  case J_JE_32:
    if (Size == 4) {
      *(Loc - 1) = 0x0f;
      *Loc = 0x84;
    } else
      *Loc = 0x74;
    break;
  case J_JNE_32:
    if (Size == 4) {
      *(Loc - 1) = 0x0f;
      *Loc = 0x85;
    } else
      *Loc = 0x75;
    break;
  case J_JG_32:
    if (Size == 4) {
      *(Loc - 1) = 0x0f;
      *Loc = 0x8f;
    } else
      *Loc = 0x7f;
    break;
  case J_JGE_32:
    if (Size == 4) {
      *(Loc - 1) = 0x0f;
      *Loc = 0x8d;
    } else
      *Loc = 0x7d;
    break;
  case J_JB_32:
    if (Size == 4) {
      *(Loc - 1) = 0x0f;
      *Loc = 0x82;
    } else
      *Loc = 0x72;
    break;
  case J_JBE_32:
    if (Size == 4) {
      *(Loc - 1) = 0x0f;
      *Loc = 0x86;
    } else
      *Loc = 0x76;
    break;
  case J_JL_32:
    if (Size == 4) {
      *(Loc - 1) = 0x0f;
      *Loc = 0x8c;
    } else
      *Loc = 0x7c;
    break;
  case J_JLE_32:
    if (Size == 4) {
      *(Loc - 1) = 0x0f;
      *Loc = 0x8e;
    } else
      *Loc = 0x7e;
    break;
  case J_JA_32:
    if (Size == 4) {
      *(Loc - 1) = 0x0f;
      *Loc = 0x87;
    } else
      *Loc = 0x77;
    break;
  case J_JAE_32:
    if (Size == 4) {
      *(Loc - 1) = 0x0f;
      *Loc = 0x83;
    } else
      *Loc = 0x73;
    break;
  default:
    error(getErrorLocation(Loc) + "unrecognized jump reloc " + Twine(Type));
  }
}

void X86_64::relocateOne(uint8_t *loc, RelType type, uint64_t val) const {
  switch (type) {
  case R_X86_64_8:
    checkIntUInt(loc, val, 8, type);
    *loc = val;
    break;
  case R_X86_64_PC8:
    checkInt(loc, val, 8, type);
    *loc = val;
    break;
  case R_X86_64_16:
    checkIntUInt(loc, val, 16, type);
    write16le(loc, val);
    break;
  case R_X86_64_PC16:
    checkInt(loc, val, 16, type);
    write16le(loc, val);
    break;
  case R_X86_64_32:
    checkUInt(loc, val, 32, type);
    write32le(loc, val);
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
    checkInt(loc, val, 32, type);
    write32le(loc, val);
    break;
  case R_X86_64_64:
  case R_X86_64_DTPOFF64:
  case R_X86_64_PC64:
  case R_X86_64_SIZE64:
  case R_X86_64_GOT64:
  case R_X86_64_GOTOFF64:
  case R_X86_64_GOTPC64:
    write64le(loc, val);
    break;
  default:
    llvm_unreachable("unknown relocation");
  }
}

RelExpr X86_64::adjustRelaxExpr(RelType type, const uint8_t *data,
                                RelExpr relExpr) const {
  if (type != R_X86_64_GOTPCRELX && type != R_X86_64_REX_GOTPCRELX)
    return relExpr;
  const uint8_t op = data[-2];
  const uint8_t modRm = data[-1];

  // FIXME: When PIC is disabled and foo is defined locally in the
  // lower 32 bit address space, memory operand in mov can be converted into
  // immediate operand. Otherwise, mov must be changed to lea. We support only
  // latter relaxation at this moment.
  if (op == 0x8b)
    return R_RELAX_GOT_PC;

  // Relax call and jmp.
  if (op == 0xff && (modRm == 0x15 || modRm == 0x25))
    return R_RELAX_GOT_PC;

  // Relaxation of test, adc, add, and, cmp, or, sbb, sub, xor.
  // If PIC then no relaxation is available.
  // We also don't relax test/binop instructions without REX byte,
  // they are 32bit operations and not common to have.
  assert(type == R_X86_64_REX_GOTPCRELX);
  return config->isPic ? relExpr : R_RELAX_GOT_PC_NOPIC;
}

// A subset of relaxations can only be applied for no-PIC. This method
// handles such relaxations. Instructions encoding information was taken from:
// "Intel 64 and IA-32 Architectures Software Developer's Manual V2"
// (http://www.intel.com/content/dam/www/public/us/en/documents/manuals/
//    64-ia-32-architectures-software-developer-instruction-set-reference-manual-325383.pdf)
static void relaxGotNoPic(uint8_t *loc, uint64_t val, uint8_t op,
                          uint8_t modRm) {
  const uint8_t rex = loc[-3];
  // Convert "test %reg, foo@GOTPCREL(%rip)" to "test $foo, %reg".
  if (op == 0x85) {
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
    loc[-1] = 0xc0 | (modRm & 0x38) >> 3; // ModR/M byte.

    // Change opcode from TEST r/m64, r64 to TEST r/m64, imm32
    // See "TEST-Logical Compare" (4-428 Vol. 2B).
    loc[-2] = 0xf7;

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
    loc[-3] = (rex & ~0x4) | (rex & 0x4) >> 2;
    write32le(loc, val);
    return;
  }

  // If we are here then we need to relax the adc, add, and, cmp, or, sbb, sub
  // or xor operations.

  // Convert "binop foo@GOTPCREL(%rip), %reg" to "binop $foo, %reg".
  // Logic is close to one for test instruction above, but we also
  // write opcode extension here, see below for details.
  loc[-1] = 0xc0 | (modRm & 0x38) >> 3 | (op & 0x3c); // ModR/M byte.

  // Primary opcode is 0x81, opcode extension is one of:
  // 000b = ADD, 001b is OR, 010b is ADC, 011b is SBB,
  // 100b is AND, 101b is SUB, 110b is XOR, 111b is CMP.
  // This value was wrote to MODRM.reg in a line above.
  // See "3.2 INSTRUCTIONS (A-M)" (Vol. 2A 3-15),
  // "INSTRUCTION SET REFERENCE, N-Z" (Vol. 2B 4-1) for
  // descriptions about each operation.
  loc[-2] = 0x81;
  loc[-3] = (rex & ~0x4) | (rex & 0x4) >> 2;
  write32le(loc, val);
}

void X86_64::relaxGot(uint8_t *loc, RelType type, uint64_t val) const {
  const uint8_t op = loc[-2];
  const uint8_t modRm = loc[-1];

  // Convert "mov foo@GOTPCREL(%rip),%reg" to "lea foo(%rip),%reg".
  if (op == 0x8b) {
    loc[-2] = 0x8d;
    write32le(loc, val);
    return;
  }

  if (op != 0xff) {
    // We are relaxing a rip relative to an absolute, so compensate
    // for the old -4 addend.
    assert(!config->isPic);
    relaxGotNoPic(loc, val + 4, op, modRm);
    return;
  }

  // Convert call/jmp instructions.
  if (modRm == 0x15) {
    // ABI says we can convert "call *foo@GOTPCREL(%rip)" to "nop; call foo".
    // Instead we convert to "addr32 call foo" where addr32 is an instruction
    // prefix. That makes result expression to be a single instruction.
    loc[-2] = 0x67; // addr32 prefix
    loc[-1] = 0xe8; // call
    write32le(loc, val);
    return;
  }

  // Convert "jmp *foo@GOTPCREL(%rip)" to "jmp foo; nop".
  // jmp doesn't return, so it is fine to use nop here, it is just a stub.
  assert(modRm == 0x25);
  loc[-2] = 0xe9; // jmp
  loc[3] = 0x90;  // nop
  write32le(loc - 1, val + 1);
}

// A split-stack prologue starts by checking the amount of stack remaining
// in one of two ways:
// A) Comparing of the stack pointer to a field in the tcb.
// B) Or a load of a stack pointer offset with an lea to r10 or r11.
bool X86_64::adjustPrologueForCrossSplitStack(uint8_t *loc, uint8_t *end,
                                              uint8_t stOther) const {
  if (!config->is64) {
    error("Target doesn't support split stacks.");
    return false;
  }

  if (loc + 8 >= end)
    return false;

  // Replace "cmp %fs:0x70,%rsp" and subsequent branch
  // with "stc, nopl 0x0(%rax,%rax,1)"
  if (memcmp(loc, "\x64\x48\x3b\x24\x25", 5) == 0) {
    memcpy(loc, "\xf9\x0f\x1f\x84\x00\x00\x00\x00", 8);
    return true;
  }

  // Adjust "lea X(%rsp),%rYY" to lea "(X - 0x4000)(%rsp),%rYY" where rYY could
  // be r10 or r11. The lea instruction feeds a subsequent compare which checks
  // if there is X available stack space. Making X larger effectively reserves
  // that much additional space. The stack grows downward so subtract the value.
  if (memcmp(loc, "\x4c\x8d\x94\x24", 4) == 0 ||
      memcmp(loc, "\x4c\x8d\x9c\x24", 4) == 0) {
    // The offset bytes are encoded four bytes after the start of the
    // instruction.
    write32le(loc + 4, read32le(loc + 4) - 0x4000);
    return true;
  }
  return false;
}

// If Intel Indirect Branch Tracking is enabled, we have to emit special PLT
// entries containing endbr64 instructions. A PLT entry will be split into two
// parts, one in .plt.sec (writePlt), and the other in .plt (writeIBTPlt).
namespace {
class IntelIBT : public X86_64 {
public:
  IntelIBT();
  void writeGotPlt(uint8_t *buf, const Symbol &s) const override;
  void writePlt(uint8_t *buf, const Symbol &sym,
                uint64_t pltEntryAddr) const override;
  void writeIBTPlt(uint8_t *buf, size_t numEntries) const override;

  static const unsigned IBTPltHeaderSize = 16;
};
} // namespace

IntelIBT::IntelIBT() { pltHeaderSize = 0; }

void IntelIBT::writeGotPlt(uint8_t *buf, const Symbol &s) const {
  uint64_t va =
      in.ibtPlt->getVA() + IBTPltHeaderSize + s.pltIndex * pltEntrySize;
  write64le(buf, va);
}

void IntelIBT::writePlt(uint8_t *buf, const Symbol &sym,
                        uint64_t pltEntryAddr) const {
  const uint8_t Inst[] = {
      0xf3, 0x0f, 0x1e, 0xfa,       // endbr64
      0xff, 0x25, 0,    0,    0, 0, // jmpq *got(%rip)
      0x66, 0x0f, 0x1f, 0x44, 0, 0, // nop
  };
  memcpy(buf, Inst, sizeof(Inst));
  write32le(buf + 6, sym.getGotPltVA() - pltEntryAddr - 10);
}

void IntelIBT::writeIBTPlt(uint8_t *buf, size_t numEntries) const {
  writePltHeader(buf);
  buf += IBTPltHeaderSize;

  const uint8_t inst[] = {
      0xf3, 0x0f, 0x1e, 0xfa,    // endbr64
      0x68, 0,    0,    0,    0, // pushq <relocation index>
      0xe9, 0,    0,    0,    0, // jmpq plt[0]
      0x66, 0x90,                // nop
  };

  for (size_t i = 0; i < numEntries; ++i) {
    memcpy(buf, inst, sizeof(inst));
    write32le(buf + 5, i);
    write32le(buf + 10, -pltHeaderSize - sizeof(inst) * i - 30);
    buf += sizeof(inst);
  }
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
  void writeGotPlt(uint8_t *buf, const Symbol &s) const override;
  void writePltHeader(uint8_t *buf) const override;
  void writePlt(uint8_t *buf, const Symbol &sym,
                uint64_t pltEntryAddr) const override;
};

class RetpolineZNow : public X86_64 {
public:
  RetpolineZNow();
  void writeGotPlt(uint8_t *buf, const Symbol &s) const override {}
  void writePltHeader(uint8_t *buf) const override;
  void writePlt(uint8_t *buf, const Symbol &sym,
                uint64_t pltEntryAddr) const override;
};
} // namespace

Retpoline::Retpoline() {
  pltHeaderSize = 48;
  pltEntrySize = 32;
  ipltEntrySize = 32;
}

void Retpoline::writeGotPlt(uint8_t *buf, const Symbol &s) const {
  write64le(buf, s.getPltVA() + 17);
}

void Retpoline::writePltHeader(uint8_t *buf) const {
  const uint8_t insn[] = {
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
  memcpy(buf, insn, sizeof(insn));

  uint64_t gotPlt = in.gotPlt->getVA();
  uint64_t plt = in.plt->getVA();
  write32le(buf + 2, gotPlt - plt - 6 + 8);
  write32le(buf + 9, gotPlt - plt - 13 + 16);
}

void Retpoline::writePlt(uint8_t *buf, const Symbol &sym,
                         uint64_t pltEntryAddr) const {
  const uint8_t insn[] = {
      0x4c, 0x8b, 0x1d, 0, 0, 0, 0, // 0:  mov foo@GOTPLT(%rip), %r11
      0xe8, 0,    0,    0,    0,    // 7:  callq plt+0x20
      0xe9, 0,    0,    0,    0,    // c:  jmp plt+0x12
      0x68, 0,    0,    0,    0,    // 11: pushq <relocation index>
      0xe9, 0,    0,    0,    0,    // 16: jmp plt+0
      0xcc, 0xcc, 0xcc, 0xcc, 0xcc, // 1b: int3; padding
  };
  memcpy(buf, insn, sizeof(insn));

  uint64_t off = pltEntryAddr - in.plt->getVA();

  write32le(buf + 3, sym.getGotPltVA() - pltEntryAddr - 7);
  write32le(buf + 8, -off - 12 + 32);
  write32le(buf + 13, -off - 17 + 18);
  write32le(buf + 18, sym.pltIndex);
  write32le(buf + 23, -off - 27);
}

RetpolineZNow::RetpolineZNow() {
  pltHeaderSize = 32;
  pltEntrySize = 16;
  ipltEntrySize = 16;
}

void RetpolineZNow::writePltHeader(uint8_t *buf) const {
  const uint8_t insn[] = {
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
  memcpy(buf, insn, sizeof(insn));
}

void RetpolineZNow::writePlt(uint8_t *buf, const Symbol &sym,
                             uint64_t pltEntryAddr) const {
  const uint8_t insn[] = {
      0x4c, 0x8b, 0x1d, 0,    0, 0, 0, // mov foo@GOTPLT(%rip), %r11
      0xe9, 0,    0,    0,    0,       // jmp plt+0
      0xcc, 0xcc, 0xcc, 0xcc,          // int3; padding
  };
  memcpy(buf, insn, sizeof(insn));

  write32le(buf + 3, sym.getGotPltVA() - pltEntryAddr - 7);
  write32le(buf + 8, in.plt->getVA() - pltEntryAddr - 12);
}

static TargetInfo *getTargetInfo() {
  if (config->zRetpolineplt) {
    if (config->zNow) {
      static RetpolineZNow t;
      return &t;
    }
    static Retpoline t;
    return &t;
  }

  if (config->andFeatures & GNU_PROPERTY_X86_FEATURE_1_IBT) {
    static IntelIBT t;
    return &t;
  }

  static X86_64 t;
  return &t;
}

TargetInfo *getX86_64TargetInfo() { return getTargetInfo(); }

} // namespace elf
} // namespace lld
