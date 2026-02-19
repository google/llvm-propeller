"""Module extension for configuring LLVM."""

load("@llvm-raw//utils/bazel:configure.bzl", "llvm_configure")

def _llvm_ext_impl(_ctx):
    llvm_configure(
        name = "llvm-project",
        targets = ["AArch64", "X86"],
    )

llvm_ext = module_extension(implementation = _llvm_ext_impl)
