# Third-party notices

The optional `IRIS_ENABLE_VULKAN` diagnostic uses Khronos Vulkan-Headers
v1.3.290 (Apache-2.0 OR MIT, with per-file exceptions recorded upstream):
https://github.com/KhronosGroup/Vulkan-Headers/tree/v1.3.290.
The archive is pinned by SHA256
`918a8bb31b8312fd649fd1d1826443da870f33f3187c16c4528aa94927409bab`.
Enabled Runtime installations include `licenses/Vulkan-Headers-LICENSE.md`
and the full license texts under `licenses/Vulkan-Headers/`. GLFW dynamically
loads the operating system's Vulkan loader; no loader or GPU driver is bundled.
The developer-only smoke executable writes PNGs using GLFW's bundled
`stb_image_write.h` (public domain / MIT); it is not installed in Runtime.

The optional `IRIS_ENABLE_PHYSX` CPU SDK slice uses NVIDIA PhysX 5.6.1
(`107.3-physx-5.6.1`, commit `5ca9f472105a90d70d957c243cb0ef36fe251a9f`),
under BSD-3-Clause: https://github.com/NVIDIA-Omniverse/PhysX/tree/5ca9f472105a90d70d957c243cb0ef36fe251a9f.
Enabled builds copy its full license to `licenses/PhysX-LICENSE.md` on Runtime
installation. SDK libraries are static and use the host dynamic MSVC CRT; no
PhysX DLL, CUDA, Blast, Flow or PVD runtime is distributed by this slice.

Iris fetches the following pinned dependencies while configuring. Their
canonical license files are copied into the `licenses` directory of release
packages; those licenses apply to the dependency code and take precedence over
this summary.

| Component | Pinned version | License | Upstream |
| --- | --- | --- | --- |
| GLFW | 3.4 | zlib/libpng | <https://github.com/glfw/glfw> |
| GLM | 1.0.3 | MIT or Happy Bunny (Modified MIT) | <https://github.com/g-truc/glm> |
| glad | 2.0.8 | Apache-2.0 / CC0 (component-dependent) | <https://github.com/Dav1dde/glad> |
| tinyobjloader | 1.0.6 | MIT | <https://github.com/tinyobjloader/tinyobjloader> |
| TinyEXR | 1.0.13 | BSD-3-Clause | <https://github.com/syoyo/tinyexr> |
| Dear ImGui | 1.92.7-docking | MIT | <https://github.com/ocornut/imgui> |
| Assimp | 6.0.5 | BSD-3-Clause; bundled components retain their own notices | <https://github.com/assimp/assimp> |
| osgw water material / Ashima Simplex noise | Copied 3D noise shader in `shaders/ocean_snoise.glsl` | MIT | <https://github.com/CaffeineViking/osgw>, <https://github.com/ashima/webgl-noise> |
| Enscape Cube Shadertoy GLSL (Thomas / @Thomas_ensc; Seascape ocean procedure by Alexander Alekseev / TDM) | User-supplied source and adapters in `shaders/third_party/enscape_cube/` and `assets/shader-scenes/enscape/` | CC BY-NC-SA 3.0 | <https://www.shadertoy.com/view/4dSBDt> |

Assimp brings the stb image loader, RapidJSON and zlib into this build. Their
license notices remain in the fetched Assimp source tree and Assimp's canonical
license file is shipped with packaged binaries.

Asset provenance and redistribution scope are recorded separately in
`ASSET_LICENSES.md`. The project's MIT license does not relicense third-party
assets.
The copied ocean shader's license texts ship in `assets/licenses/`.

MSVC-built Windows packages also include the compiler-provided release CRT
redistributable DLLs discovered by CMake's `InstallRequiredSystemLibraries`.
These Microsoft runtime components are not covered by the project's MIT license.
Windows system libraries and the OpenGL driver are supplied by the operating
system and GPU installation, rather than copied from the development machine.
