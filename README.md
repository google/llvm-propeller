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
