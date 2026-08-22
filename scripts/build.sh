#!/usr/bin/env bash
# Build Capicola module for Schwung (ARM64)
#
# Automatically uses Docker for cross-compilation if needed.
# Set CROSS_PREFIX to skip Docker (e.g., for native ARM builds).
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
IMAGE_NAME="move-anything-builder-cpp"

# Check if we need Docker
if [ -z "$CROSS_PREFIX" ] && [ ! -f "/.dockerenv" ]; then
    echo "=== Capicola Module Build (via Docker) ==="
    echo ""

    # Build Docker image if needed
    if ! docker image inspect "$IMAGE_NAME" &>/dev/null; then
        echo "Building Docker image (first time only)..."
        docker build -t "$IMAGE_NAME" -f "$SCRIPT_DIR/Dockerfile" "$REPO_ROOT"
        echo ""
    fi

    # Run build inside container
    echo "Running build..."
    docker run --rm \
        -v "$REPO_ROOT:/build" \
        -u "$(id -u):$(id -g)" \
        -w /build \
        "$IMAGE_NAME" \
        ./scripts/build.sh

    echo ""
    echo "=== Done ==="
    exit 0
fi

# === Actual build (runs in Docker or with cross-compiler) ===
CROSS_PREFIX="${CROSS_PREFIX:-aarch64-linux-gnu-}"

cd "$REPO_ROOT"

echo "=== Building Capicola Module ==="
echo "Cross prefix: $CROSS_PREFIX"

# Create build directories
mkdir -p build
mkdir -p dist/capicola

# Apply patches to a pristine copy of the vendored lib (src/dsp/lib stays untouched)
echo "Applying patches to vendored lib..."
./scripts/apply_patches.sh

# Compile DSP plugin (with aggressive optimizations for CM4)
echo "Compiling DSP plugin..."
${CROSS_PREFIX}g++ -std=c++17 -Ofast -shared -fPIC \
    -march=armv8-a -mtune=cortex-a72 \
    -fomit-frame-pointer -fno-stack-protector \
    -DNDEBUG \
    src/dsp/capicola_plugin.cpp src/dsp/capicola_engine.cpp src/dsp/capicola_params.cpp \
    -o build/capicola.so -Ibuild/lib -Isrc/dsp \
    -lm

# Copy files to dist (use cat to avoid ExtFS deallocation issues with Docker)
echo "Packaging..."
cat src/module.json > dist/capicola/module.json
[ -f src/help.json ] && cat src/help.json > dist/capicola/help.json
cat build/capicola.so > dist/capicola/capicola.so
chmod +x dist/capicola/capicola.so

# Create tarball for release
cd dist
tar -czvf capicola-module.tar.gz capicola/
cd ..

echo ""
echo "=== Build Complete ==="
echo "Output: dist/capicola/"
echo "Tarball: dist/capicola-module.tar.gz"
echo ""
echo "To install on Move:"
echo "  ./scripts/install.sh"
