set(LLVM_TARGET_DEFINITIONS Options.td)
tablegen(LLVM Options.inc -gen-opt-parser-defs)
add_public_tablegen_target(ELFOptionsTableGen)

if(NOT LLD_BUILT_STANDALONE)
  set(tablegen_deps intrinsics_gen)
endif()

find_package(Protobuf)
if( Protobuf_FOUND )
  add_compile_definitions(PROPELLER_PROTOBUF GOOGLE_PROTOBUF_NO_RTTI)
  include_directories(${Protobuf_INCLUDE_DIRS})
  protobuf_generate_cpp(PROPELLER_PROTO_SRCS PROPELLER_PROTO_HDRS propeller_cfg.proto)
  set(PROPELLER_PROTOBUF_SRCS Propeller/Protobuf.cpp)
  set(PROPELLER_PROTOBUF_LIBS ${Protobuf_LIBRARIES})
endif()

add_lld_library(lldELF
  AArch64ErrataFix.cpp
  Arch/AArch64.cpp
  Arch/AMDGPU.cpp
  Arch/ARM.cpp
  Arch/AVR.cpp
  Arch/Hexagon.cpp
  Arch/Mips.cpp
  Arch/MipsArchTree.cpp
  Arch/MSP430.cpp
  Arch/PPC.cpp
  Arch/PPC64.cpp
  Arch/RISCV.cpp
  Arch/SPARCV9.cpp
  Arch/X86.cpp
  Arch/X86_64.cpp
  ARMErrataFix.cpp
  CallGraphSort.cpp
  DWARF.cpp
  Driver.cpp
  DriverUtils.cpp
  EhFrame.cpp
  ICF.cpp
  InputFiles.cpp
  InputSection.cpp
  LTO.cpp
  LinkerScript.cpp
  MapFile.cpp
  MarkLive.cpp
  OutputSections.cpp
  Propeller.cpp
  PropellerBBReordering.cpp
  PropellerCfg.cpp
  PropellerFuncOrdering.cpp
  PropellerBBReordering.cpp
  ${PROPELLER_PROTO_SRCS}
  ${PROPELLER_PROTOBUF_SRCS}
  Relocations.cpp
  ScriptLexer.cpp
  ScriptParser.cpp
  SymbolTable.cpp
  Symbols.cpp
  SyntheticSections.cpp
  Target.cpp
  Thunks.cpp
  Writer.cpp

  LINK_COMPONENTS
  ${LLVM_TARGETS_TO_BUILD}
  BinaryFormat
  BitWriter
  Core
  DebugInfoDWARF
  LTO
  MC
  Object
  Option
  Support

  LINK_LIBS
  lldCommon
  ${LLVM_PTHREAD_LIB}
  ${PROPELLER_PROTOBUF_LIBS}
  
  DEPENDS
  ELFOptionsTableGen
  ${tablegen_deps}
  )
