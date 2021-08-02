#!/usr/bin/env bash

# Echo each command
set -x

# Exit on error.
set -e

# Core deps.
sudo apt-get install wget

# Install conda+deps.
wget https://github.com/conda-forge/miniforge/releases/latest/download/Miniforge3-Linux-ppc64le.sh -O miniconda.sh
export deps_dir=$HOME/local
export PATH="$HOME/miniconda/bin:$PATH"
bash miniconda.sh -b -p $HOME/miniconda
conda create -y -q -p $deps_dir c-compiler cxx-compiler cmake eigen nlopt boost-cpp tbb tbb-devel
source activate $deps_dir

# Create the build dir and cd into it.
mkdir build
cd build

# GCC build with address sanitizer.
cmake ../ -DCMAKE_BUILD_TYPE=Debug -DPAGMO_BUILD_TESTS=yes -DPAGMO_WITH_EIGEN3=yes -DPAGMO_WITH_NLOPT=yes -DCMAKE_CXX_FLAGS="-fsanitize=address"
make -j2 VERBOSE=1
# Run the tests, except the fork island which
# gives spurious warnings in the address sanitizer.
# Also, enable the custom suppression file for ASAN
# in order to suppress spurious warnings from TBB code.
LSAN_OPTIONS=suppressions=$TRAVIS_BUILD_DIR/tools/lsan.sup ctest -j4 -V -E fork_island

set +e
set +x
