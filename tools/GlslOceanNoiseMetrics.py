"""Fixed-input spatial/temporal high-frequency proxies, not noise ground truth."""
import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image


def luminance(path):
    rgb = np.asarray(Image.open(path).convert("RGB"), dtype=np.float64) / 255.0
    if rgb.shape != (720, 1280, 3):
        raise ValueError(f"Expected 1280x720: {path}")
    return rgb @ np.array([0.2126, 0.7152, 0.0722])


def highpass(x):
    average = sum(x[j:j+x.shape[0]-2, i:i+x.shape[1]-2]
                  for j in range(3) for i in range(3)) / 9.0
    return x[1:-1, 1:-1] - average


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    result = {}
    region = np.s_[450:690, 100:1180]
    for mode, prefix in (("off", "off"), ("on", "final")):
        a = luminance(args.directory / f"{prefix}-1.25.png")
        b = luminance(args.directory / f"{prefix}-1.27.png")
        result[mode] = {
            "meanDisplayLuminance": float(a[region].mean()),
            "spatialHighpassRms": float(np.sqrt(np.mean(highpass(a)[region]**2))),
            "temporalHighpassDeltaRms": float(np.sqrt(np.mean((highpass(a)-highpass(b))[region]**2))),
        }
    off, on = result["off"], result["on"]
    if on["spatialHighpassRms"] > 0.8 * off["spatialHighpassRms"]:
        raise ValueError("Spatial high-frequency proxy did not improve by 20%")
    if on["temporalHighpassDeltaRms"] > 0.75 * off["temporalHighpassDeltaRms"]:
        raise ValueError("Temporal high-frequency proxy did not improve by 25%")
    if not 0.8 <= on["meanDisplayLuminance"] / off["meanDisplayLuminance"] <= 1.3:
        raise ValueError("Image brightness changed outside the guard")
    result["scope"] = "Display RGB high-frequency proxy includes real wave detail; not physical noise variance."
    (args.directory / "noise-metrics.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
