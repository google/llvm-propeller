# Copyright 2024 The Propeller Authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""A utility to list the symbols of a binary."""

load("@bazel_tools//tools/cpp:toolchain_utils.bzl", "find_cpp_toolchain", "use_cpp_toolchain")

def _list_symbols(ctx):
    cc_toolchain = find_cpp_toolchain(ctx)
    output = ctx.actions.declare_file(ctx.label.name)
    binary = ctx.file.src
    command = [cc_toolchain.nm_executable, "--radix=" + _ADDRESS_RADIX[ctx.attr.address_radix]]
    if ctx.attr.defined_only:
        command.append("--defined-only")
    if ctx.attr.print_size:
        command.append("--print-size")
    command.append(binary.path)

    ctx.actions.run_shell(
        outputs = [output],
        inputs = [binary],
        command = " ".join(command) + " 1>" + output.path,
        progress_message = "Listing symbols in %s for %s" % (
            binary.short_path,
            output.short_path,
        ),
        tools = cc_toolchain.all_files,
        toolchain = "@bazel_tools//tools/cpp:toolchain_type",
    )
    return DefaultInfo(
        files = depset([output]),
        runfiles = ctx.runfiles(files = [output]),
    )

_ADDRESS_RADIX = {
    "dec": "d",
    "hex": "x",
    "oct": "o",
}

list_symbols = rule(
    fragments = ["cpp"],
    implementation = _list_symbols,
    attrs = {
        "src": attr.label(
            doc = "Binaries to list labels of.",
            allow_single_file = True,
        ),
        "defined_only": attr.bool(default = False),
        "address_radix": attr.string(
            default = "hex",
            values = _ADDRESS_RADIX.keys(),
        ),
        "print_size": attr.bool(default = False),
        "_cc_toolchain": attr.label(default = "@bazel_tools//tools/cpp:current_cc_toolchain"),
    },
    toolchains = use_cpp_toolchain(),
)
