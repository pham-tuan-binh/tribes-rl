#!/usr/bin/env bash
# Head-to-head eval between two checkpoints (defaults: newest vs oldest of the
# most recent run). Usage: match_lk.sh [ckpt_a] [ckpt_b] [num_games]
set -euo pipefail
SP=~/venvs/puffer/lib/python3.12/site-packages
export PATH=~/venvs/puffer/bin:/usr/local/cuda/bin:$PATH
export LD_LIBRARY_PATH=$SP/nvidia/nccl/lib:$SP/nvidia/cudnn/lib

DIR=~/PufferLib/checkpoints/polytopia/$(ls -t ~/PufferLib/checkpoints/polytopia/ | head -1)
A=${1:-$(ls "$DIR"/*.bin | sort | tail -1)}
B=${2:-$(ls "$DIR"/*.bin | sort | head -1)}
N=${3:-512}
echo "A (newest): $A"
echo "B (oldest): $B"
cd ~/PufferLib
puffer match polytopia --load-model-path "$A" --load-enemy-model-path "$B" --num-games "$N"
