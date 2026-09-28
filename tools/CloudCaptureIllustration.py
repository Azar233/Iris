"""Reproduce the C7 documentation figure after cloud-determinism-acceptance.
Requires Pillow, NumPy and Matplotlib; no runtime renderer dependency.
"""
from pathlib import Path
import struct
import numpy as np
from PIL import Image, ImageDraw, ImageFont
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

root = Path(__file__).resolve().parent.parent
raw = (root / "assets/clouds/transport-v1.cloudlut").read_bytes()
values = np.array(struct.unpack("<8193I", raw[28:]), dtype=float) / (1 << 24)
x = np.linspace(0, 10, 3000)
y = np.interp(x, np.arange(8193) / 256, values)
fig, axes = plt.subplots(1, 2, figsize=(10, 3.4), dpi=140)
axes[0].plot(x, np.exp(-x), label="Analytic exp(-tau)", lw=2)
axes[0].plot(x, y, "--", label="Offline Q24 LUT", lw=1)
axes[0].set_ylabel("Transmission")
axes[0].legend()
axes[1].plot(x, np.abs(y - np.exp(-x)))
axes[1].set_ylabel("Absolute approximation error")
for axis in axes:
    axis.set_xlabel("Optical depth")
    axis.grid(alpha=.2)
fig.tight_layout()
plot_path = root / "build-ci-msvc/cloud-c7-transport-plot.png"
fig.savefig(plot_path)
plt.close(fig)
shot = Image.open(root / "build-ci-msvc/cloud-determinism-acceptance/first/frame_0000.png").convert("RGB").resize((960, 540))
plot = Image.open(plot_path).convert("RGB").resize((960, 326))
out = Image.new("RGB", (960, 966), (24, 29, 35))
draw = ImageDraw.Draw(out)
font = ImageFont.truetype("C:/Windows/Fonts/msyh.ttc", 23)
draw.text((18, 12), "C7：离线云噪声 + 透射率 LUT，固定时间与输入清单", font=font, fill="white")
out.paste(shot, (0, 50))
draw.text((18, 605), "LUT 近似误差 < 2.1e-6；输入变化会拒绝后续发布", font=font, fill="white")
out.paste(plot, (0, 640))
out.save(root / "docs/media/p1a-cloud-c7-transport-capture.png")
