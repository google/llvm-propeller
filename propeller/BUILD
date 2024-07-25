load("@rules_license//rules:license.bzl", "license")

# Propeller, a profile-guided, relinking optimizer for warehouse-scale applications.
package_group(
    name = "default_visibility",
    packages = [
        "//propeller/...",
    ],
)

package(
    default_applicable_licenses = [":license"],
    default_visibility = [":default_visibility"],
)

# Define the license for this package, which is used as the default license
# for all targets in this package based on default_applicable_licenses above.
license(name = "license")

# Legacy deprecated licenses rule.
# Should be kept until transition to new license rules is complete.
licenses(["notice"])

exports_files(["LICENSE"])

###############
#  Libraries  #
###############
cc_library(
    name = "binary_address_branch",
    hdrs = ["binary_address_branch.h"],
    visibility = [
        ":default_visibility",
    ],
    deps = ["@abseil-cpp//absl/strings:str_format"],
)
