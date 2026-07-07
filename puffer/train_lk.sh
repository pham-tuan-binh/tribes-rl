#!/usr/bin/env bash
# Training environment setup + launch for the lk box (RTX 5090, CUDA 13).
# Usage (on lk): ~/polytopia-rl/puffer/train_lk.sh [extra puffer args...]
# One-time setup already done: PufferLib@4.0 in ~/PufferLib, venv in
# ~/venvs/puffer (torch cu130 + pufferlib -e), apt: python3.12-dev libomp-dev
# ccache, and ~/lib symlinks for libnccl.so/libcudnn.so from the torch wheels.
set -euo pipefail

SP=~/venvs/puffer/lib/python3.12/site-packages
export PATH=~/venvs/puffer/bin:/usr/local/cuda/bin:$PATH
export CUDA_HOME=/usr/local/cuda
export LIBRARY_PATH=$HOME/lib                       # build-time nccl/cudnn
export LD_LIBRARY_PATH=$SP/nvidia/nccl/lib:$SP/nvidia/cudnn/lib

cd ~/PufferLib
bash ~/polytopia-rl/puffer/install_into_pufferlib.sh ~/PufferLib ~/polytopia-rl
# PLAYERS=4 trains the 4-player env (rebuilds _C with that player count)
export EXTRA_CFLAGS="-DENV_PLAYERS=${PLAYERS:-2}"
./build.sh polytopia
exec puffer train polytopia "$@"
