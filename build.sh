#!/bin/bash
#
# Convex C++ Client Build Script
#
# Usage:
#   ./build.sh              # Build library and examples
#   ./build.sh test         # Build and run tests
#   ./build.sh clean        # Clean build directory
#   ./build.sh release      # Release build (optimized)
#   ./build.sh install      # Install to system (requires sudo)
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
BUILD_TYPE="Debug"
RUN_TESTS=false
CLEAN=false
INSTALL=false

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        test)
            RUN_TESTS=true
            shift
            ;;
        clean)
            CLEAN=true
            shift
            ;;
        release)
            BUILD_TYPE="Release"
            shift
            ;;
        install)
            INSTALL=true
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [test|clean|release|install]"
            echo ""
            echo "Commands:"
            echo "  (none)    Build library and examples"
            echo "  test      Build and run tests"
            echo "  clean     Remove build directory"
            echo "  release   Build with optimizations"
            echo "  install   Install to system (may require sudo)"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Clean
if $CLEAN; then
    echo "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
    echo "Done."
    exit 0
fi

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure
echo "Configuring (${BUILD_TYPE})..."
cmake \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCONVEX_BUILD_TESTS=ON \
    -DCONVEX_BUILD_EXAMPLES=ON \
    ..

# Build
echo "Building..."
cmake --build . --parallel $(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

echo ""
echo "Build complete!"
echo ""
echo "Outputs:"
echo "  Library:  $BUILD_DIR/libconvex.a"
echo "  Examples: $BUILD_DIR/simple_subscribe"
echo "            $BUILD_DIR/mutation_example"
if $RUN_TESTS; then
    echo "  Tests:    $BUILD_DIR/convex_tests"
fi

# Run tests
if $RUN_TESTS; then
    echo ""
    echo "Running tests..."
    ./convex_tests
fi

# Install
if $INSTALL; then
    echo ""
    echo "Installing..."
    sudo cmake --install .
    echo "Installed to /usr/local"
fi
