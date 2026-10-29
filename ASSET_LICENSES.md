# Asset licenses and provenance

## Shipped in binary release packages

| Asset group | Provenance | License |
| --- | --- | --- |
| `assets/environments/kloofendal_48d_partly_cloudy_puresky_4k.exr` | Poly Haven, “Kloofendal 48d Partly Cloudy (Pure Sky)”; see the adjacent README | CC0 1.0 |
| `cube.obj`, `sphere.obj`, material/glass/prism/skinning regression fixtures and their textures | Created or procedurally generated for Iris | MIT, under the project license |
| `assets/shader-scenes/orbit`, `feedback`, `main-image`, `textures`, `sequence`, `mouse` | JSON / GLSL / PNG / raw examples created for Iris; adjacent license copies | MIT |
| `assets/shader-materials/checker` | Model fragment material example created for Iris; adjacent license copy | MIT |
| `assets/shader-scenes/enscape` | Enscape Cube by Thomas / @Thomas_ensc, Seascape by Alexander Alekseev / TDM; Iris package adapters and deterministic replacement textures; adjacent notices | CC BY-NC-SA 3.0 |
| `assets/icons/iris-source.png`, `iris-icon.png`, `iris.ico` | AI-generated iris artwork, revised and selected for Iris; source and export instructions in `assets/icons/README.md` | Distributed under the project MIT license |
| `assets/readme/*.png` | Captured from the Iris editor; the studio scene uses the CC0 Poly Haven models listed below | MIT, under the project license |
| `assets/models/polyhaven/*` | Five 1K glTF showcase models from Poly Haven; authors and source URLs are recorded in the adjacent README | CC0 1.0 |
| `assets/benchmarks/m3c/sponza/*` | Intel Sponza 2022 by Frank Meinl, sponsored by Anton Kaplanyan; derived opaque glTF with 512 px textures; full source credit and license adjacent | CC BY 4.0, with the source's additional notice |
| `assets/benchmarks/m3c/stanford/*` | Stanford Bunny and classic Cyberware Dragon, Stanford Computer Graphics Laboratory; repaired closed geometry for glass tests; provenance and changes adjacent | Research use and free redistribution with acknowledgement; commercial use or products for sale require Stanford permission |
| `shaders/ocean_snoise.glsl` | 3D Simplex noise distributed with [osgw](https://github.com/CaffeineViking/osgw), originally by Ashima Arts / Stefan Gustavson; license copies in `assets/licenses/` | MIT |

## Source-tree-only reference models

`assets/benchmarks/khronos-sponza/` contains the unmodified Khronos
glTF-Sample-Assets Crytek Sponza model for local evaluation, including its
cloth and alpha-masked foliage. Upstream revision and per-file hashes are
recorded in `SOURCE.json`; the original `LICENSE.md`, metadata and CRYENGINE
license reference are adjacent. Model assets are covered by the upstream
CRYENGINE Limited License Agreement, not Iris MIT or the Intel Sponza CC BY
license. This directory, including its Iris scene, is excluded from CPack
runtime installation; redistribution permission has not been established by
this integration.

The following legacy/reference models remain useful for local importer checks,
but are deliberately excluded from CPack binary releases:

- `bunny.obj` and `bunny_hole.dae`: Stanford Bunny derivatives. Stanford permits
  research use and free redistribution with acknowledgement, but restricts
  commercial product use without permission. Source: Stanford Computer Graphics
  Laboratory, Stanford 3D Scanning Repository.
- `dragon2.dae`: Stanford Dragon derivative. The same Stanford repository terms
  apply.
- `cow.dae`, `spot_*.obj` / `spot_*.mtl`, and their `spot_*` / `hmap.jpg`
  textures: legacy sample assets whose exact
  upstream revision and redistribution grant have not yet been established.

No rights beyond the original owners' terms are claimed for these reference
models. Do not include them in a commercial or redistributable build until their
provenance is replaced with a verifiable permissive source.

The original procedural room (`src/pathtracer/AcceptanceScene.cpp`),
`emissive_test.*` fixtures, and the retained regression resources in
`tests/baselines/` were created for Iris and are covered by the project MIT license.
