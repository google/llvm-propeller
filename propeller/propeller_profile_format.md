# Propeller Profile Format

This document describes the basic block sections profile format used by
Propeller and read by LLVM's `BasicBlockSectionsProfileReader`. This profile is
used to guide optimizations like basic block reordering, function splitting, and
inlining.

The profile format exists in two versions: v0 and v1. If no version is specified
on the first line (e.g., `v1`), v0 is assumed.

## Version 1 Format

The version 1 format is line-based, with each line starting with a single
character specifier that determines the type of information on that line. Lines
starting with `@` are ignored and can be used for comments.

Profile for each function is encoded as follows:

```
m <module_name>
f <function_name_1> <function_name_2> ...
c <bb_id_1> <bb_id_2> <bb_id_3>
c <bb_id_4> <bb_id_5>
...
```

### `m`: Module Name

A line starting with `m` is optional and specifies a module name:

```
m <module_name>
```

This is used to distinguish internal-linkage functions with the same name that
may appear in different modules. If provided, the following function profile
only applies to a function with a matching name and module. Alternatively,
compiling with `-funique-internal-linkage-names` ensures unique names for
internal linkage functions, avoiding the need for module name disambiguation.

### `f`: Function Name

A line starting with `f` specifies one or more function names or aliases:

```
f <function_name_1> <function_name_2> ...
```

All profile information following this line (until the next `f` line) applies to
this function. The first name is considered the canonical name, and any other
names are treated as aliases.

### `c`: Basic Block Cluster

A line starting with `c` specifies a cluster of basic blocks:

```
c <bb_id_1> <bb_id_2> ...
```

This specifies a cluster of basic blocks and the internal order in which they
should be placed in the same section. A function can have multiple clusters,
specified by multiple `c` lines. Each `c` line starts a new cluster.

Basic block IDs can refer to original blocks or cloned blocks. A cloned block is
identified by `<original_block_id>.<clone_id>`, e.g., `3.1`. Original blocks are
specified with just their ID, e.g., `3`.

### `p`: Cloning Path

A line starting with `p` specifies a cloning path:

```
p <bb_id_1> <bb_id_2> <bb_id_3> ...
```

This instructs the compiler to clone basic blocks along a path. The first two
blocks of a cloning path specify the edge along which the path is cloned. For
instance, `p 1 3 4` instructs that blocks 3 and 4 must be cloned along the edge
`1->3`. The cloned blocks are then referenced in cluster specifications (e.g.,
`3.1`, `4.1`).

### `g`: CFG Profile

A line starting with `g` specifies control-flow graph profile information for
the function:

```
g <src1>:<cnt>,<sink1>:<cnt1>,... <src2>:<cnt>,<sink1>:<cnt1>,...
```

For each node, its CFG profile is encoded as
`<src>:<count>,<sink_1>:<count_1>,<sink_2>:<count_2>,...`, where `<src>` is the
ID of a source basic block, `<count>` is its total execution count, and
`<sink_i>:<count_i>` represents an edge to a successor block `<sink_i>` with
edge count `<count_i>`.

### `h`: Basic Block Hash

A line starting with `h` specifies basic block hashes:

```
h <bb_id1>:<hash1> <bb_id2>:<hash2> ...
```

Each `<bb_id>:<hash>` pair provides the hash for a given basic block ID in hex
format. This hash is computed by the [`MachineBlockHashInfo` pass](https://www.llvm.org/doxygen/classllvm_1_1MachineBlockHashInfo.html).

### `t`: Prefetch Target

A line starting with `t` specifies a prefetch target:

```
t <bbid>,<subblock_index>
```

This instructs the compiler to emit a prefetch symbol for the given target. A
prefetch target is specified by a pair `<bbid>,<subblock_index>` where `bbid`
specifies the target basic block and `subblock_index` is a zero-based index.
Subblock 0 refers to the region at the beginning of the block up to the first
callsite. Subblock `i > 0` refers to the region immediately after the `i`-th
callsite up to the `i+1`-th callsite (or the end of the block). The prefetch
target symbol is always emitted at the beginning of the subblock. If a prefetch
target's BBID does not map to any basic block in the function during code
generation, the target symbol is emitted at the beginning of the function.

### `i`: Prefetch Hint

A line starting with `i` specifies a prefetch hint:

```
i <site_bbid>,<site_callsite_index> <target_func>,<target_bbid>,<target_callsite_index>
```

This instructs the compiler to insert a prefetch hint instruction at a given
site for a given target. The prefetch site is specified as
`<site_bbid>,<site_callsite_index>` similar to prefetch targets. The prefetch
target is specified as a triple
`<target_func>,<target_bbid>,<target_callsite_index>`.

### Example

The following profile lists two cloning paths for function `bar` and places the
total 9 blocks within two clusters.

```
f main
f bar
p 1 3 4 # cloning path 1
p 4 2 # cloning path 2
c 1 3.1 4.1 6 # basic block cluster 1
c 0 2 3 4 2.1 5 # basic block cluster 2
```

********************************************************************************

Function `bar` before and after cloning with basic block clusters shown:

********************************************************************************

```
                              ....      ..............
    0 -------+                : 0 :---->: 1 ---> 3.1 :
    |        |                : | :     :........ |  :
    v        v                : v :             : v  :
+--> 2 --> 5  1   ~~~~~~>  +---: 2 :             : 4.1: cluster 1
|    |        |            |   : | :             : |  :
|    v        |            |   : v .......       : v  :
|    3 <------+            |   : 3 <--+  :       : 6  :
|    |                     |   : |    |  :       :....:
|    v                     |   : v    |  :
+--- 4 ---> 6              |   : 4    |  :
                          |   : |    |  :
                          |   : v    |  :
                          |   :2.1---+  : cluster 2
                          |   : | ......:
                          |   : v :
                          +-->: 5 :
                              ....
```

********************************************************************************

The following example illustrates prefetch targets and hints. A basic block in
function "foo" with BBID 10 and two call instructions (call_A, call_B) is
conceptually split into subblocks, with the prefetch target symbol emitted at
the beginning of each subblock.

```
+----------------------------------+
| __llvm_prefetch_target_foo_10_0: | <--- Subblock 0 (before call_A)
|  Instruction 1                   |
|  Instruction 2                   |
|  call_A (Callsite 0)             |
| __llvm_prefetch_target_foo_10_1: | <--- Subblock 1 (after call_A,
|                                  |                  before call_B)
|  Instruction 3                   |
|  call_B (Callsite 1)             |
| __llvm_prefetch_target_foo_10_2: | <--- Subblock 2 (after call_B,
|                                  |                  before call_C)
|  Instruction 4                   |
+----------------------------------+
```

A prefetch hint specified in function "bar" as `i 120,1 foo,10,2` results in a
hint inserted after the first call in block #120 of bar:

```
BB #120 in bar:
+----------------------------------------------------+
| Instruction 1                                      |
| call_C (Callsite 1)                                |
| code_prefetch __llvm_prfetch_target_foo_10_2       |
| Instruction 2                                      |
+----------------------------------------------------+
```

## Version 0 Format

Version 0 format uses `!` as a prefix for function names and `!!` for clusters.
It does not support code prefetching or cloning. A file with basic block
sections for all of function main and three blocks for function foo (of which 1
and 2 are placed in a cluster) looks like this: (Profile for function foo is
only loaded when its debug-info filename matches 'path/to/foo_file.cc').

```
!main
!foo M=path/to/foo_file.cc
!!1 2
!!4
```

Function names can be specified as aliases separated by `/`, e.g. `!foo/bar`. A
debug-info filename can be specified for each function with `M=<path>` to allow
distinguishing internal-linkage functions of the same name. Each line starting
with `!!` defines a cluster of basic blocks.
