// Rasterize the vendored Lucide SVGs into a compact ImGui texture atlas.
// Run with Node.js and sharp installed: node tools/generate-editor-icons.mjs
import {createRequire} from 'node:module';
import {readFile, writeFile} from 'node:fs/promises';
import {fileURLToPath} from 'node:url';
import path from 'node:path';

const require = createRequire(import.meta.url);
const sharp = require(process.env.MYRENDERER_SHARP_PATH || 'sharp');
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const names = ['play', 'pause', 'step-forward', 'rotate-ccw',
               'file-box', 'folder', 'box', 'image'];
const cell = 64;
const overlays = [];
for (const [index, name] of names.entries()) {
    const source = await readFile(path.join(root, 'assets/icons/lucide', `${name}.svg`), 'utf8');
    const white = source.replace(/currentColor/g, '#ffffff');
    const png = await sharp(Buffer.from(white)).resize(44, 44).png().toBuffer();
    overlays.push({input: png, left: (index % 4) * cell + 10,
                   top: Math.floor(index / 4) * cell + 10});
}
const atlas = await sharp({create: {width: cell * 4, height: cell * 2,
                                     channels: 4, background: '#00000000'}})
    .composite(overlays).png().toBuffer();
await writeFile(path.join(root, 'assets/icons/editor-atlas.png'), atlas);
