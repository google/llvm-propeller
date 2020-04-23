This directory contains core source files of propeller framework.

LLVM RFC: http://lists.llvm.org/pipermail/llvm-dev/2019-September/135393.html

# 1. About the propeller framework.

Propeller is framework that, with the aid of embedded information
in ELF obejct files, does optimization at the linking phase.

# 2. How (current) propeller works.

## [The compiler part]

   Each basicblock is represented using a standalone elf section.

## [The profile part]

   A propeller-format profile ('propfile') is generated, which
   contains counters for jumps/calls/returns of each bb.

## [The lld part]

   LLD handles execution to propeller (Propeller.h is the interface
   that works with lld), which does a few things for each ELF object
   file:

   1. generates control flow graph (CFG) for each function, this is
      possible because each basicblock section resides in a single elf
      section and we use relocation entries to determine jump/call
      targets of each basicblock section
   
   2. parses propfile (the propeller-format profile), apply
      basicblock counters, edge counters to CFGs
   
   3. passes information in (2) to BBReordering and FunctionOrdering
      pass, which generates list of optimal basicblock symbol ordering
   
   4. Propeller feeds the list (generated in (3)) back to lld, and lld
      continues to work as usual.

# Some misconections:

  - Propeller only works on top of ThinLTO (regular PGO, csPGO, etc).

    The above statement is not correct. Propeller works on any
    binaries and brings performance improvement on top of the binary,
    regardless of how the binaries are optimized.

  - Propeller is an extension to -ffunction-sections and -fdata-sections.

    The above statement is not correct. Propeller is a *general*
    framework, it will be able to do things like reodering individual
    insns, remove/insert spill insns, etc. And we have plans for
    future optimizations based on propeller framework (not on bb
    sections).

    However, current Propeller's main optimization
    (function-splitting, function-reordering, basicblock-reordering)
    is done via -fbasic-block-sections, which is an extension of
    -ffunction-sections.

# 3. Where are other propeller related files?

  - ../LinkerPropeller.h ../LinkerPropeller.cpp interfaces that
    interact with lld.
  
  - ../../include/lld/Common/PropellerCommon.h common interfaces that
    are used by create_llvm_prof.
