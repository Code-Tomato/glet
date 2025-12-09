#!/bin/bash

# Get the directory where the script is located
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
# Get the project root (parent of scripts directory)
PROJECT_ROOT="$( cd "$SCRIPT_DIR/.." && pwd )"
BUILD_DIR="$PROJECT_ROOT/bin"

mkdir -p $BUILD_DIR

cd $BUILD_DIR

cmake -DCMAKE_BUILD_TYPE=Debug "$PROJECT_ROOT"

make -j 32 standalone_scheduler
