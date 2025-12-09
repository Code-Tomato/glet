#!/bin/bash
#
# Install dependencies for the GPU scheduler
#
# Required dependencies:
#   - CMake 3.12+ (for building)
#   - Boost libraries (boost_program_options, boost_system)
#   - Google Logging library (glog)

set -e

echo "Installing dependencies for GPU scheduler..."

# Update package list
sudo apt-get -y update

# Install build essentials
sudo apt-get -y install build-essential

# Install CMake (check if already installed and version)
if command -v cmake &> /dev/null; then
    CMAKE_VERSION=$(cmake --version | head -n1 | cut -d' ' -f3)
    REQUIRED_VERSION="3.12"
    if [ "$(printf '%s\n' "$REQUIRED_VERSION" "$CMAKE_VERSION" | sort -V | head -n1)" != "$REQUIRED_VERSION" ]; then
        echo "CMake version $CMAKE_VERSION found, but 3.12+ is required."
        echo "Installing CMake 3.12+..."
        sudo apt-get -y install cmake
    else
        echo "CMake $CMAKE_VERSION is already installed (>= 3.12)"
    fi
else
    echo "CMake not found. Installing..."
    sudo apt-get -y install cmake
fi

# Install Boost libraries (required for boost_program_options and boost_system)
echo "Installing Boost libraries..."
sudo apt-get -y install libboost-all-dev

# Install Google Logging library (glog)
echo "Installing Google Logging library..."
sudo apt-get -y install libgoogle-glog-dev

echo ""
echo "Dependencies installed successfully!"
echo ""
echo "To build the scheduler, run:"
echo "  ./scripts/build.sh"
echo ""
