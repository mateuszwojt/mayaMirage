# Mirage Maya Plugin

A Maya integration for the [Mirage](https://github.com/mateuszwojt/Mirage) path tracer, targeting **Maya 2026/2027** on **macOS (Apple Silicon) and Linux (x86_64)** and Mirage's Slang/Vulkan (and CPU) renderer backends.

The plugin registers Mirage as a classic Maya renderer (`Render > Render Current Frame`, `Render Settings`, and batch rendering via `Render`/`mayabatch`), with a progressive, cancellable Render View, real Maya scene translation (materials, lights, instancing, motion blur), AOVs, and file output.

## Requirements

- Maya 2026 or 2027, with the standalone Maya devkit package extracted somewhere on disk (Maya 2023+ no longer bundles the devkit inside the application install).
- macOS on Apple Silicon — Mirage's GPU backend (Slang shaders via slang-rhi/MoltenVK) only builds and runs on macOS.
- A built and installed [`mirage`](../mirage) **v1.1.0 or later** (the `find_package(Mirage CONFIG)` package under `mirage/install/`) — this plugin uses v1.1.0 APIs (`Material::opacity`/`normalTextureIndex`, `Mesh::verticesEnd`/`tangents`, `Scene::AddInstancer`) that don't exist in v1.0.0.

## Building

```sh
export DEVKIT_LOCATION=/path/to/devkitBase   # contains include/ and lib/ directly

cmake -S . -B build \
    -DMirage_DIR=/path/to/mirage/install/lib/cmake/Mirage \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

`DEVKIT_LOCATION` (or the `MAYA_DEVKIT_ROOT` CMake cache variable) points at the extracted Maya devkit; `Mirage_DIR` points at Mirage's installed CMake package. Installing (`cmake --install build`) copies the built plugin and `scripts/MirageMaya` into Maya's per-user plug-ins/scripts directories.

## CI / Releases

[`.github/workflows/build.yml`](.github/workflows/build.yml) builds the plugin for every `{macos-arm64, linux-x86_64} x {maya2026, maya2027}` combination:

- [`ci.yml`](.github/workflows/ci.yml) runs it on every push/PR to `main` as a compile check (nothing is published).
- [`release.yml`](.github/workflows/release.yml) runs it when a `vX.Y.Z` tag is pushed, then attaches the built `MirageMaya-<tag>-maya<version>-<os>-<arch>.zip` archives (each containing `plug-ins/MirageMaya.{bundle,so}` and `scripts/MirageMaya/`) to a GitHub Release for that tag.

Two dependencies are fetched at build time rather than checked in:

- **Mirage** — a public, prebuilt install tarball is downloaded from [Mirage's own GitHub Releases](https://github.com/mateuszwojt/Mirage/releases), pinned to the version named in the checked-in [`MIRAGE_VERSION`](MIRAGE_VERSION) file. Bump that file (and open a PR) to pick up a newer Mirage.
- **The Maya devkit** — proprietary, so it isn't fetchable from a public URL. Autodesk's own downloads (`Autodesk_Maya_<version>_DEVKIT_{Mac.dmg,Linux.tgz}`) are uploaded as-is as assets on the `devkits` release in a private `mateuszwojt/maya-devkits` repo, downloaded via `gh release download` using the `MAYA_DEVKIT_PAT` repo secret (a fine-grained PAT with read access to that repo's releases). The macOS `.dmg` is mounted with `hdiutil`; both archive types are then searched for wherever `include/maya/MFn.h` actually lands, since Autodesk doesn't guarantee the same inner-folder name across archives.

## Usage

Load the plugin (`Plug-in Manager` or `loadPlugin MirageMaya`), then select **Mirage Renderer** as the active renderer in `Render Settings`. The Mirage tab there (and the `MirageRendererGlobalsNode`'s own Attribute Editor page) exposes:

- **Mode**: CPU/GPU backend, render mode (normals/complexity/path trace), reconstruction filter.
- **Sampling**: exposure, firefly limit, sample count, max ray depth, depth of field.
- **Motion Blur**: enable + shutter-open/close (frame offsets around the rendered frame).
- **Lighting**: a global intensity calibration scale for the point/spot/directional light approximations (see Limitations).
- **AOVs**: depth/normal/prim-ID, viewable in the Render View's own AOV channel selector.
- **Output**: image format for batch/file output (PNG/JPG/BMP/TGA).
- **Instancing**: a global off-switch for shared-mesh instancing, useful for isolating issues.

Interactive renders (`Render Current Frame`) run on a background thread and refine progressively in the Render View; cancel via the Render View's own cancel control. Batch renders (`Render -s <start> -e <end>`, or `mayabatch`) render synchronously and write files using Maya's own Common Render Settings naming/padding convention.

## What's translated

- **Geometry**: triangulated meshes, correctly handling non-uniform scale/shear, per-face UV seams (unwelded to match Mirage's per-vertex UV model), and per-shading-group splitting (a mesh with multiple shading groups becomes multiple Mirage primitives).
- **Instancing**: Maya DAG instances whose world transform is rigid (translate/rotate/uniform-scale) share one object-space mesh and BVH across all instances — genuine GPU-memory-shared instancing, not per-instance geometry copies. Instances with shear or non-uniform scale (which Mirage's transform can't represent) fall back to a baked private copy. MASH's Instancer node and nParticle instancing (Maya's native `instancer` DAG node) are translated separately via Mirage's own procedural point-instancer (see Limitations for its single-prototype v1 scope).
- **Motion blur**: rigid-transform blur (two time-sampled transforms) for every instance, plus deforming (2-keyframe) per-vertex blur for meshes with deformer history (skinCluster/blendShape/etc. — auto-detected, no extra setup needed) via Mirage's per-primitive vertex snapshot. The two compose independently.
- **Materials**: `standardSurface` (primary target — base color, metalness, roughness, specular, coat, sheen, transmission, subsurface, emission, IOR, opacity) plus legacy Lambert/Blinn/Phong. Connected `file` textures are supported for base color/roughness/metalness/opacity, plus tangent-space normal maps via the standard `file → bump2d (Tangent Space Normals) → normalCamera` network.
- **Lights**: point, spot, directional, area, and ambient. Mirage has no native point/spot/directional light type — see Limitations.
- **Output**: 8-bit PNG/JPG/BMP/TGA, plus 32-bit float EXR (preserves full render dynamic range, no tonemapping/quantization).

## Limitations

Mirage's own light model is just a sky gradient plus emissive geometry — there's no dedicated point/spot/directional/area light type. Translated lights are therefore physically approximate to varying degrees:

- **Area lights** are the best match (Mirage's model _is_ emissive geometry).
- **Point/spot lights** become small emissive spheres (a standard path-tracer workaround for a zero-area source); spot cone falloff isn't representable at all.
- **Directional lights** are the weakest approximation — a large emissive sphere placed far away, which doesn't reproduce correct shadow softness.
- Use the **Light Intensity Scale** setting to calibrate brightness; there is no native physical-unit conversion.

Other known gaps:

- Opacity is stochastic cutout (a surface is either fully there or fully not, per sample), not alpha blending — there's no partial-coverage/soft-edge transparency.
- Normal mapping is tangent-space only and requires the mesh to have UVs; object-space normal maps and classic height-field bump aren't supported.
- No Viewport 2.0 / interactive-viewport IPR — interactivity is via the classic, progressive Render View only.
- MASH/particle-instancer translation is single-prototype, single-material only: an `instancer` node referencing more than one prototype object (or a multi-shading-group prototype mesh) only translates its first mesh prototype's geometry and first-found material, with a once-per-instancer warning — matches Mirage's own `PointInstancer` v1 scope (see `PointInstancer.h`). Instancer prototype objects are also not hidden automatically by this plugin the way Maya's own viewport does, so a visible (non-hidden) prototype renders both as itself and as every instanced copy.
- Per-instance shading-group overrides on an instanced shape aren't supported — shading is queried once and applied to all instances of that shape.
- Deforming motion blur doesn't deform shading normals (still barycentric-interpolated from the static, start-of-shutter normals regardless of ray time) — a Mirage v1 scope limit on hit-position/silhouette motion, not shading quality.

## Architecture

- `RenderProcedure` (`MPxCommand`) parses the classic-renderer invocation and drives scene translation; it is destroyed by Maya immediately after `doIt()` returns (non-undoable commands aren't retained), so nothing that needs to outlive a single call lives here.
- `RenderWorker` is a long-lived singleton owning the `RenderSession` (Mirage `Scene` + active `Renderer` backend) and the background render thread — it has to be a singleton independent of `RenderProcedure`'s lifetime for exactly the reason above.
- `RenderSession` handles CPU/GPU backend selection (with automatic fallback) and recreate-gating (Mirage has no scene-dirty-tracking of its own; structural changes require rebuilding the renderer).
- `translators/` (`SceneTranslator`, `MeshTranslator`, `MaterialTranslator`, `LightTranslator`, `InstancerTranslator`, `MayaTransformUtils`) walk the DAG once per render and translate meshes, materials, lights, and particle/MASH instancers into the scene.
- `ImageWriter` handles 8-bit (PNG/JPG/BMP/TGA) and 32-bit float (EXR) file output for batch rendering.
