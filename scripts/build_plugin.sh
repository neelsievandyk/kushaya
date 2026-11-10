#!/bin/bash
# =============================================================================
# build_plugin.sh - builds the Kushaya plugin and installs it.
#
# Run from anywhere:
#   ./scripts/build_plugin.sh
#
# The first run downloads JUCE and compiles everything (about 5-10 minutes).
# Later runs only rebuild what changed (usually under a minute).
#
# Result:
#   ~/Library/Audio/Plug-Ins/VST3/Kushaya.vst3   <- Studio One loads this
#   build.nosync/plugin/Kushaya_artefacts/Release/Standalone/Kushaya.app
# =============================================================================

set -e   # stop at the first error

# Work from the project root, wherever the script was called from.
cd "$(dirname "$0")/.."

# Everything generated goes in build.nosync/. The ".nosync" ending tells
# iCloud Drive not to upload it.
BUILD_DIR="build.nosync"
JUCE_VERSION="9.0.5"

if ! command -v cmake >/dev/null; then
    echo "CMake is not installed. Install it with:  brew install cmake"
    exit 1
fi

# Download JUCE (the plugin framework) the first time.
if [ ! -d "$BUILD_DIR/JUCE" ]; then
    echo "Downloading JUCE $JUCE_VERSION..."
    mkdir -p "$BUILD_DIR"
    git clone --depth 1 --branch "$JUCE_VERSION" \
        https://github.com/juce-framework/JUCE.git "$BUILD_DIR/JUCE"
fi

# "Configure": CMake reads plugin/CMakeLists.txt and prepares the build.
cmake -S plugin -B "$BUILD_DIR/plugin" -DCMAKE_BUILD_TYPE=Release

# "Build": compile everything, using all CPU cores.
cmake --build "$BUILD_DIR/plugin" --config Release --parallel "$(sysctl -n hw.ncpu)"

echo
echo "Done."
echo "  Plugin:     ~/Library/Audio/Plug-Ins/VST3/Kushaya.vst3"
echo "  Standalone: $BUILD_DIR/plugin/Kushaya_artefacts/Release/Standalone/Kushaya.app"
