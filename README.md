# LLVM Propeller

Propeller is a profile-guided, relinking optimizer for warehouse-scale
applications. It is built on top of LLVM and provides a framework for computing
whole-program optimizations for applications built with LLVM.

_For artifact evaluation, see [ArtifactEvaluation/README.md](ArtifactEvaluation/README.md)_

## Quickstart

### Prerequisites and dependencies

| Operating System | Version  |
| --- | --- |
| Ubuntu | 22.04 or newer |

While the Propeller build system automatically pulls in most of its dependencies,
you will need to install a few packages manually:

```
# Common dependencies
sudo apt install -y wget lsb-release software-properties-common gnupg

# Propeller builds with Clang 19.0.0 or newer.
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
sudo ./llvm.sh 19
export CC=clang-19
export CXX=clang++-19

# For the CMake build
sudo apt-get update && sudo apt-get install -y \
    libelf-dev \
    libssl-dev \
    libzstd-dev
```

### Building Propeller from source

The Propeller repository provides both CMake and Bazel build configurations.
The CMake build requires CMake 3.24 or newer, and the Bazel build requires
Bazel 5.0 or newer.

#### CMake
```
cmake -G Ninja -B build
ninja -C build generate_propeller_profiles

# Build and run tests (optional)
ninja -C build
ninja -C build test
```

#### Bazel
```
bazel build //propeller:generate_propeller_profiles

# Build and run tests (optional)
bazel test //propeller/...:all
```

### Generating a Propeller profile
```
./generate_propeller_profiles \
    --binary=/path/to/profiled/binary \
    --profile=/path/to/input/profile.txt \
    --cc_profile=/path/to/out/cc_profile.txt \
    --ld_profile=/path/to/out/ld_profile.txt
```
