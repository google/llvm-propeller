load("//tools/build_defs/license:license.bzl", "license")

# Propeller, a profile-guided, relinking optimizer for warehouse-scale applications.
package(
    default_applicable_licenses = [":license"],
    default_visibility = [
        "//third_party/llvm_propeller:__subpackages__",
    ],
)

# Define the license for this package, which is used as the default license
# for all targets in this package based on default_applicable_licenses above.
license(name = "license")

# Legacy deprecated licenses rule.
# Should be kept until transition to new license rules is complete.
licenses(["notice"])

exports_files(["LICENSE"])
