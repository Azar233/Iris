# Asset licenses and provenance

## Shipped in binary release packages

| Asset group | Provenance | License |
| --- | --- | --- |
| `assets/environments/kloofendal_48d_partly_cloudy_puresky_4k.exr` | Poly Haven, “Kloofendal 48d Partly Cloudy (Pure Sky)”; see the adjacent README | CC0 1.0 |
| `cube.obj`, `sphere.obj`, material/glass/prism/skinning regression fixtures and their textures | Created or procedurally generated for Iris | MIT, under the project license |
| `assets/icons/iris-source.png`, `iris-icon.png`, `iris.ico` | AI-generated iris artwork, revised and selected for Iris; source and export instructions in `assets/icons/README.md` | Distributed under the project MIT license |
| `assets/readme/*.png` | Captured from the Iris editor; the studio scene uses the CC0 Poly Haven models listed below | MIT, under the project license |
| `assets/models/polyhaven/*` | Five 1K glTF showcase models from Poly Haven; authors and source URLs are recorded in the adjacent README | CC0 1.0 |
| `shaders/ocean_snoise.glsl` | 3D Simplex noise distributed with [osgw](https://github.com/CaffeineViking/osgw), originally by Ashima Arts / Stefan Gustavson; license copies in `assets/licenses/` | MIT |

## Source-tree-only reference models

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
