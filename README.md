# Propeller: Profile Guided Optimizing Large Scale LLVM-based Relinker

## Background

We recently evaluated Facebook’s BOLT, a Post Link Optimizer
framework, on large google benchmarks and noticed that it improves key
performance metrics of these benchmarks by 2% to 6%, which is pretty
impressive as this is over and above a baseline binary already heavily
optimized with ThinLTO + PGO. Furthermore, BOLT is also able to
improve the performance of binaries optimized via Context-Sensitive
PGO. While ThinLTO + PGO is also profile guided and does very
aggressive performance optimizations, there is more room for
performance improvements due to profile approximations while applying
the transformations. BOLT uses exact profiles from the final binary
and is able to fill the gaps left by ThinLTO + PGO. The performance
improvements due to BOLT come from basic block layout, function
reordering and function splitting.

While BOLT does an excellent job of squeezing extra performance from highly optimized
binaries with optimizations such as code layout, it has these major issues:

1. It does not take advantage of distributed build systems.

2. It has scalability issues and to rewrite a binary with a ~300M text segment size:

   * Memory foot-print is 70G.
   
   * It takes more than 10 minutes to rewrite the binary.

Similar to Full LTO, BOLT’s design is monolithic as it disassembles
the original binary, optimizes and rewrites the final binary in one
process. This limits the scalability of BOLT and the memory and time
overhead shoots up quickly for large binaries.

Inspired by the performance gains and to address the scalability issue
of BOLT, we went about designing a scalable infrastructure that can
perform BOLT-like post-link optimizations. In this RFC, we discuss our
system, “Propeller”, which can perform profile guided link time binary
optimizations in a scalable way and is friendly to distributed build
systems. Our system leverages the existing capabilities of the
compiler tool-chain and is not a stand alone tool. Like BOLT, our
system boosts the performance of optimized binaries via link-time
optimizations using accurate profiles of the binary. We discuss the
Propeller system and show how to do the whole program basic block
layout using Propeller.

Propeller does whole program basic block layout at link time via basic
block sections. We have added support for having each basic block in
its own section which allows the linker to do arbitrary reorderings of
basic blocks to achieve any desired fine-grain code layout which
includes block layout, function splitting and function reordering.
Our experiments on large real-world applications and SPEC with code
layout show that Propeller can optimize as effectively as BOLT, with
just 20% of its memory footprint and time overhead.

An LLVM branch with propeller patches is available in the git
repository here: https://github.com/google/llvm-propeller/ We will
upload patches for review for the various elements



This directory and its subdirectories contain source code for LLVM,
a toolkit for the construction of highly optimized compilers,
optimizers, and runtime environments.

The README briefly describes how to get started with building LLVM.
For more information on how to contribute to the LLVM project, please
take a look at the
[Contributing to LLVM](https://llvm.org/docs/Contributing.html) guide.

## Getting Started with the LLVM System

Taken from https://llvm.org/docs/GettingStarted.html.

### Overview

Welcome to the LLVM project!

The LLVM project has multiple components. The core of the project is
itself called "LLVM". This contains all of the tools, libraries, and header
files needed to process intermediate representations and converts it into
object files.  Tools include an assembler, disassembler, bitcode analyzer, and
bitcode optimizer.  It also contains basic regression tests.

C-like languages use the [Clang](http://clang.llvm.org/) front end.  This
component compiles C, C++, Objective C, and Objective C++ code into LLVM bitcode
-- and from there into object files, using LLVM.

Other components include:
the [libc++ C++ standard library](https://libcxx.llvm.org),
the [LLD linker](https://lld.llvm.org), and more.

### Getting the Source Code and Building LLVM

The LLVM Getting Started documentation may be out of date.  The [Clang
Getting Started](http://clang.llvm.org/get_started.html) page might have more
accurate information.

This is an example workflow and configuration to get and build the LLVM source:

1. Checkout LLVM (including related subprojects like Clang):

     * ``git clone https://github.com/llvm/llvm-project.git``

     * Or, on windows, ``git clone --config core.autocrlf=false
    https://github.com/llvm/llvm-project.git``

2. Configure and build LLVM and Clang:

     * ``cd llvm-project``

     * ``mkdir build``

     * ``cd build``

     * ``cmake -G <generator> [options] ../llvm``

        Some common generators are:

        * ``Ninja`` --- for generating [Ninja](https://ninja-build.org)
          build files. Most llvm developers use Ninja.
        * ``Unix Makefiles`` --- for generating make-compatible parallel makefiles.
        * ``Visual Studio`` --- for generating Visual Studio projects and
          solutions.
        * ``Xcode`` --- for generating Xcode projects.

        Some Common options:

        * ``-DLLVM_ENABLE_PROJECTS='...'`` --- semicolon-separated list of the LLVM
          subprojects you'd like to additionally build. Can include any of: clang,
          clang-tools-extra, libcxx, libcxxabi, libunwind, lldb, compiler-rt, lld,
          polly, or debuginfo-tests.

          For example, to build LLVM, Clang, libcxx, and libcxxabi, use
          ``-DLLVM_ENABLE_PROJECTS="clang;libcxx;libcxxabi"``.

        * ``-DCMAKE_INSTALL_PREFIX=directory`` --- Specify for *directory* the full
          pathname of where you want the LLVM tools and libraries to be installed
          (default ``/usr/local``).

        * ``-DCMAKE_BUILD_TYPE=type`` --- Valid options for *type* are Debug,
          Release, RelWithDebInfo, and MinSizeRel. Default is Debug.

        * ``-DLLVM_ENABLE_ASSERTIONS=On`` --- Compile with assertion checks enabled
          (default is Yes for Debug builds, No for all other build types).

      * Run your build tool of choice!

        * The default target (i.e. ``ninja`` or ``make``) will build all of LLVM.

        * The ``check-all`` target (i.e. ``ninja check-all``) will run the
          regression tests to ensure everything is in working order.

        * CMake will generate build targets for each tool and library, and most
          LLVM sub-projects generate their own ``check-<project>`` target.

        * Running a serial build will be *slow*.  To improve speed, try running a
          parallel build. That's done by default in Ninja; for ``make``, use
          ``make -j NNN`` (NNN is the number of parallel jobs, use e.g. number of
          CPUs you have.)

      * For more information see [CMake](https://llvm.org/docs/CMake.html)

Consult the
[Getting Started with LLVM](https://llvm.org/docs/GettingStarted.html#getting-started-with-llvm)
page for detailed information on configuring and compiling LLVM. You can visit
[Directory Layout](https://llvm.org/docs/GettingStarted.html#directory-layout)
to learn about the layout of the source code tree.

