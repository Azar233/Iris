"""Compose genuine renderer evidence and a deliberately slowed, captioned reel."""
import argparse
import json
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

parser = argparse.ArgumentParser()
parser.add_argument('--build', default='build-ci-msvc')
args = parser.parse_args()
root = Path(__file__).resolve().parent.parent
build = root / args.build
shots = build / 'm2d-hero'
summary = json.loads((build / 'm2d-scene-job/summary.json').read_text(encoding='utf-8-sig'))
sequence = Path(summary['RunDirectory']) / 'first'
media = root / 'docs/media'
readme = root / 'assets/readme'
font = ImageFont.truetype('C:/Windows/Fonts/msyh.ttc', 20)

def panel(pairs, name):
    canvas = Image.new('RGB', (1280, len(pairs)*392), '#181c23')
    draw = ImageDraw.Draw(canvas)
    for row, pair in enumerate(pairs):
        for column, (file, title) in enumerate(pair):
            with Image.open(file) as image:
                canvas.paste(image.convert('RGB').resize((640, 360), Image.Resampling.LANCZOS), (column*640, row*392+32))
            draw.text((column*640+8, row*392+3), title, font=font, fill='white')
    canvas.save(media/name)

panel([[(root/'docs/media/m2c-noon-high.png', 'M2-C：近景偏平滑'), (shots/'noon-high.png', 'M2-D：过滤的多尺度短波')],
       [(shots/'noon-low.png', 'Low：960 × 540'), (shots/'noon-high.png', 'High：1280 × 720')]], 'm2d-detail-quality.png')
panel([[(shots/'water-off.png', '同机位：Water Off'), (shots/'noon-high.png', 'Water On')],
       [(shots/'clouds-off.png', 'Clouds Off'), (shots/'noon-high.png', 'Clouds On')]], 'm2d-on-off.png')
for name in ('noon-high', 'sunset-high', 'night-high'):
    with Image.open(shots/(name+'.png')) as image:
        image.save(media/('m2d-'+name+'.png'))
with Image.open(shots/'cloud-shape/high.ppm') as image:
    canvas = Image.new('RGB', (960,580), '#181c23')
    canvas.paste(image.resize((960,540), Image.Resampling.NEAREST), (0,40))
    ImageDraw.Draw(canvas).text((8,5), 'CPU 云透射率参考 | 96 × 54 | 白：透射率 1；黑：0', font=font, fill='white')
    canvas.save(media/'m2d-cloud-transmission-debug.png')
with Image.open(shots/'noon-high.png') as image:
    image.save(readme/'m2d-hero.png')
frames, delays = [], []
for index in range(24):
    canvas = Image.new('RGB', (640,392), '#181c23')
    with Image.open(sequence/f'frame_{index:04}.png') as image:
        canvas.paste(image.convert('RGB').resize((640,360), Image.Resampling.LANCZOS), (0,32))
    ImageDraw.Draw(canvas).text((8,3), f'Native Scene | frame {index:02}/23 | source {index/24:.3f} s @ 24 FPS', font=font, fill='white')
    frames.append(canvas)
    delays.append(1200 if index in (0,15,23) else 100)
frames[0].save(readme/'m2d-native-reel.gif', save_all=True, append_images=frames[1:], duration=delays, loop=0, disposal=2)
with Image.open(readme/'m2d-native-reel.gif') as reel:
    if reel.n_frames != 24:
        raise RuntimeError('Incomplete reel')
print('M2-D figures and 24-frame slowed reel: PASS')
