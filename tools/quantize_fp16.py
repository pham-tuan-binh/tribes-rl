#!/usr/bin/env python3
"""Quantize a flat fp32 policy checkpoint to fp16.

Halves the weights download; the site expands fp16 back to fp32 in JS before
handing the buffer to the WASM engine, so inference is unchanged (fp16 is
effectively lossless for these magnitudes).

Usage: quantize_fp16.py checkpoint.bin web/weights/latest.bin
"""
import sys

import numpy as np

src, dst = sys.argv[1], sys.argv[2]
a = np.fromfile(src, dtype=np.float32)
a.astype(np.float16).tofile(dst)
print(f"{src}: {a.nbytes / 1e6:.1f} MB fp32 -> {a.nbytes / 2e6:.1f} MB fp16 ({a.size} params)")
