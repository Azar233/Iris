"""Plot accepted pose caches; requires matplotlib only for this documentation tool."""
import argparse
import json
from pathlib import Path
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

parser = argparse.ArgumentParser()
parser.add_argument("cache_directory", type=Path)
parser.add_argument("output", type=Path)
args = parser.parse_args()
names = ["14_physics_free_fall", "15_physics_bounce", "16_physics_stack"]
fig, axes = plt.subplots(1, 3, figsize=(12.8, 3.6), layout="constrained")
for index, (name, axis) in enumerate(zip(names, axes)):
    data = json.loads((args.cache_directory / f"{name}-cache.json").read_text(encoding="utf-8-sig"))
    times = [frame["frame"] / data["key"]["framesPerSecond"] for frame in data["frames"]]
    for identity in ([2] if index < 2 else [2, 3, 4]):
        heights = [next(entity["translation"][1] for entity in frame["entities"]
                        if entity["id"] == identity) for frame in data["frames"]]
        axis.plot(times, heights, lw=2, label=f"Entity {identity}")
    if index == 0:
        axis.plot(times, [10 - 0.5 * 9.81 * t * t for t in times], "--", color="0.35", label="Analytic")
    else:
        axis.axhline(0.5, color="0.5", ls=":", lw=1, label="Ground + half size")
    axis.set(title=["Free fall", "Sphere bounce", "Three-box stack"][index],
             xlabel="Time (s)", ylabel="Center height (m)")
    axis.grid(alpha=0.2)
    axis.legend(fontsize=8)
fig.suptitle("PhysX 5.6.1 / PGS / fixed step 1/120 s / output 30 FPS", fontsize=11)
args.output.parent.mkdir(parents=True, exist_ok=True)
for extension in ("png", "svg"):
    fig.savefig(args.output.with_suffix("." + extension), dpi=160)
plt.close(fig)
