#!/bin/bash
# =============================================================================
# retrain.sh - teach the plugin your recordings, then rebuild it.
#
# Use this after adding or replacing .wav files in data/kick, data/snare or
# data/hihat:
#   ./scripts/retrain.sh
#
# It runs scripts/train_plugin_model.py (which rewrites plugin/core/Model.h)
# and then scripts/build_plugin.sh. Restart your DAW afterwards so it loads
# the new plugin.
# =============================================================================

set -e
cd "$(dirname "$0")/.."

# The training script needs numpy and scikit-learn. They live in their own
# small Python environment inside build.nosync/, created here the first time.
# (The older ./venv is inside iCloud Drive, which makes it very slow to load.)
VENV="build.nosync/venv"

if [ ! -x "$VENV/bin/python" ]; then
    echo "Setting up the training environment (first run only)..."
    mkdir -p build.nosync
    python3 -m venv "$VENV"
    "$VENV/bin/pip" install --quiet numpy scikit-learn
fi

"$VENV/bin/python" scripts/train_plugin_model.py

echo
./scripts/build_plugin.sh
