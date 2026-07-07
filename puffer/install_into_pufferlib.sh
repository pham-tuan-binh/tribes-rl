#!/usr/bin/env bash
# Assemble ocean/polytopia inside a PufferLib checkout from this repo.
# Usage: puffer/install_into_pufferlib.sh <pufferlib_dir> [polytopia_repo_dir]
set -euo pipefail

PUF=${1:?pufferlib dir}
REPO=${2:-$(cd "$(dirname "$0")/.." && pwd)}

mkdir -p "$PUF/ocean/polytopia"
cp "$REPO/puffer/binding.c" "$PUF/ocean/polytopia/"
cp "$REPO/puffer/polytopia_env.h" "$PUF/ocean/polytopia/"
rm -rf "$PUF/ocean/polytopia/core"
mkdir -p "$PUF/ocean/polytopia/core"
cp "$REPO"/core/*.h "$PUF/ocean/polytopia/core/"
cp "$REPO/puffer/polytopia.ini" "$PUF/config/polytopia.ini"
echo "installed into $PUF/ocean/polytopia"
