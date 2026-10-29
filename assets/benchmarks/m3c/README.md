# M3-C ray tracing benchmarks

`sponza-atrium.myscene` uses the Intel Sponza 2022 atrium scene by Frank Meinl,
sponsored by Anton Kaplanyan. Source: [Intel GPU Research Samples](https://www.intel.com/content/www/us/en/developer/topic-technology/graphics-research/samples.html).
The source's attribution and full CC BY 4.0 license are preserved in
`sponza/credits_license.txt`. This derived glTF retains the source geometry
except four primitives using the `dirt_decal` BLEND material, which this GPU
path tracer deliberately rejects; its 72 PNG textures were resized to a
maximum edge of 512 pixels. The source `light_bulb` emissive factor was set
to zero: its 8,098 tiny triangles made the finite CPU reference very noisy,
and the scene instead uses four fixed point lights. Geometry remains intact.
`sponza/DERIVATION.json` records the changes and original/output sizes.
The original archive remains available from Intel.

`stanford-glass.myscene` and `stanford-opaque.myscene` use the Stanford Bunny
and the classic Cyberware Stanford Dragon from the [Stanford 3D Scanning
Repository](https://graphics.stanford.edu/data/3Dscanrep/). The bunny is the
`bun_zipper_res2.ply` reconstruction from `stanford-bunny.zip`; the dragon is
`dragon_vrip_res3.ply.gz` from `dragon_recon.tar.gz`. The upstream dragon
README is preserved as `stanford/SOURCE-README.txt`. The meshes were repaired
to closed, outward oriented surfaces for volume absorption; triangle and
topology counts before/after are in `stanford/DERIVATION.json`. The glass
materials, opaque controls, and tiled backdrop were authored for Iris.

Credit Stanford Computer Graphics Laboratory when using these models.
Stanford permits research use and free redistribution with acknowledgement;
commercial uses or use in products for sale require its permission. These
terms apply to the model geometry even when packed in an Iris ZIP. The models
are not relicensed by the Iris MIT license. The Dragon is presented as a
stationary intact model in keeping with Stanford's usage guidance.

Jobs `assets/renderjobs/22_gpu_sponza_atrium.renderjob`,
`23_gpu_stanford_glass.renderjob`, and `24_gpu_stanford_opaque.renderjob`
fix the camera, 160 x 90 resolution, seed 20261026, eight bounces, AOVs,
and PNG/EXR outputs. They intentionally live outside `assets/scenes/fixtures`.
