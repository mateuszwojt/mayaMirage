# Mirage Maya Plugin

A Maya integration for the [Mirage](https://github.com/mateuszwojt/Mirage) path tracer, targeting **Maya 2026/2027** on **macOS (Apple Silicon), Linux (x86_64), and Windows (x86_64)** and Mirage's Slang/Vulkan (and CPU) renderer backends — Slang/MoltenVK on macOS, native Vulkan on Linux and Windows.

The plugin registers Mirage as a classic Maya renderer (`Render > Render Current Frame`, `Render Settings`, and batch rendering via `Render`/`mayabatch`), with a progressive, cancellable Render View, real Maya scene translation (materials, lights, instancing, motion blur), AOVs, and file output.

## Requirements

- Maya 2026 or 2027, with the standalone Maya devkit package extracted somewhere on disk (Maya 2023+ no longer bundles the devkit inside the application install).
- macOS (Apple Silicon), Linux (x86_64), or Windows (x86_64) — Mirage's GPU backend (Slang shaders via slang-rhi) builds and runs on all three as of Mirage v1.3.0's Windows support: MoltenVK on macOS, native Vulkan on Linux and Windows.
- A built and installed [`mirage`](../mirage) **v1.3.0 or later** (the `find_package(Mirage CONFIG)` package under `mirage/install/`) — this plugin uses v1.3.0 APIs (`Mirage::kAovAlbedo`/`AovBuffers::albedo`, `Mirage::ViewTransform`/`Mirage::ApplyViewTransform`, `Options::accumulateAovs`, `NonLocalMeansFilter`'s guide-buffer params + `Options::enableDenoise`) on top of v1.2.0's (`Camera::fStop`/`focalLength`/`sensorWidth`/`sensorHeight`, `Scene::lights`/`Mirage::PunctualLight`, `Primitive::eRect`, `Skylight.h`'s `BakePreethamSky`, `Texture::GenerateMips`/`udimGridWidth`/`udimGridHeight`) and v1.1.0's (`Material::opacity`/`normalTextureIndex`, `Mesh::verticesEnd`/`tangents`, `Scene::AddInstancer`), none of which exist in v1.0.0.

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

[`.github/workflows/build.yml`](.github/workflows/build.yml) builds the plugin for every `{macos-arm64, linux-x86_64, windows-x86_64} x {maya2026, maya2027}` combination:

- [`ci.yml`](.github/workflows/ci.yml) runs it on every push/PR to `main` as a compile check (nothing is published).
- [`release.yml`](.github/workflows/release.yml) runs it when a `vX.Y.Z` tag is pushed, then attaches the built `MirageMaya-<tag>-maya<version>-<os>-<arch>.zip` archives (each containing `plug-ins/MirageMaya.{bundle,so,mll}` and `scripts/MirageMaya/`) to a GitHub Release for that tag.

Two dependencies are fetched at build time rather than checked in:

- **Mirage** — a public, prebuilt install archive (`.tar.gz` on macOS/Linux, `.zip` on Windows — matching Mirage's own release workflow's per-platform packaging) is downloaded from [Mirage's own GitHub Releases](https://github.com/mateuszwojt/Mirage/releases), pinned to the version named in the checked-in [`MIRAGE_VERSION`](MIRAGE_VERSION) file. Bump that file (and open a PR) to pick up a newer Mirage. On Windows, Mirage's Slang runtime DLLs (installed under its package's `lib/`) are copied alongside the staged `MirageMaya.mll` — Windows has no rpath equivalent, so the plugin's runtime dependencies have to live next to it instead.
- **The Maya devkit** — proprietary, so it isn't fetchable from a public URL. Autodesk's own downloads (`Autodesk_Maya_<version>_DEVKIT_{Mac.dmg,Linux.tgz,Windows.zip}`) are uploaded as-is as assets on the `devkits` release in a private `mateuszwojt/maya-devkits` repo, downloaded via `gh release download` using the `MAYA_DEVKIT_PAT` repo secret (a fine-grained PAT with read access to that repo's releases). The macOS `.dmg` is mounted with `hdiutil`; all three archive types are then searched for wherever `include/maya/MFn.h` actually lands, since Autodesk doesn't guarantee the same inner-folder name across archives.

## Usage

Load the plugin (`Plug-in Manager` or `loadPlugin MirageMaya`), then select **Mirage Renderer** as the active renderer in `Render Settings`. The Mirage tab there (and the `MirageRendererGlobalsNode`'s own Attribute Editor page) exposes:

- **Mode**: CPU/GPU backend, render mode (normals/complexity/path trace), reconstruction filter.
- **Sampling**: exposure, firefly limit, sample count, max ray depth, depth of field.
- **Motion Blur**: enable + shutter-open/close (frame offsets around the rendered frame).
- **Lighting**: a global intensity calibration scale for the point/spot light soft-shadow approximation (see Limitations).
- **Sky**: gradient (default) or Mirage's analytic Preetham sky, baked from whichever directional light is in the scene (a fixed default sun angle if there isn't one), plus a turbidity control.
- **AOVs**: depth/normal/prim-ID/albedo, viewable in the Render View's own AOV channel selector.
- **Output**: image format for batch/file output (PNG/JPG/BMP/TGA/EXR) plus a view transform (None/Filmic/ACES-like/sRGB Display) applied to LDR output and the Render View preview alike — see What's translated.
- **Instancing**: a global off-switch for shared-mesh instancing, useful for isolating issues.
- **Denoise (NLM)**: Mirage's post-process Non-Local-Means denoise, applied once to the final frame using the albedo/normal AOVs as edge-aware guide buffers (requested automatically, independent of their own AOV checkboxes above).

Interactive renders (`Render Current Frame`) run on a background thread and refine progressively in the Render View; cancel via the Render View's own cancel control. Batch renders (`Render -s <start> -e <end>`, or `mayabatch`) render synchronously and write files using Maya's own Common Render Settings naming/padding convention.

## What's translated

- **Geometry**: triangulated meshes, correctly handling non-uniform scale/shear, per-face UV seams (unwelded to match Mirage's per-vertex UV model), and per-shading-group splitting (a mesh with multiple shading groups becomes multiple Mirage primitives).
- **Instancing**: Maya DAG instances whose world transform is rigid (translate/rotate/uniform-scale) share one object-space mesh and BVH across all instances — genuine GPU-memory-shared instancing, not per-instance geometry copies. Instances with shear or non-uniform scale (which Mirage's transform can't represent) fall back to a baked private copy. MASH's Instancer node and nParticle instancing (Maya's native `instancer` DAG node) are translated separately via Mirage's own procedural point-instancer (see Limitations for its single-prototype v1 scope).
- **Motion blur**: rigid-transform blur (two time-sampled transforms) for every instance, plus deforming (2-keyframe) per-vertex blur for meshes with deformer history (skinCluster/blendShape/etc. — auto-detected, no extra setup needed) via Mirage's per-primitive vertex snapshot. The two compose independently.
- **Materials**: `standardSurface` (primary target — base color, metalness, roughness, specular, coat, sheen, transmission, subsurface, emission, IOR, opacity) plus legacy Lambert/Blinn/Phong. Connected `file` textures are supported for base color/roughness/metalness/opacity, plus tangent-space normal maps via the standard `file → bump2d (Tangent Space Normals) → normalCamera` network. Every loaded texture gets a full mip chain automatically (trilinear-filtered at render time — no plugin-side setup needed), and UDIM-tiled (`uvTilingMode = "UDIM (Mari)"`) file textures are composited into one atlas texture at load time rather than only the first tile being picked up.
- **Camera**: physical focal length/sensor size/f-stop drive depth-of-field aperture sizing; field of view is still derived from Maya's own Film Fit/render-aspect-aware frustum, not the physical focal-length formula (see Limitations for why).
- **Lights**: point and spot lights are true delta (or, with a small default radius, soft-shadow) punctual lights; directional lights are true parallel-ray punctual lights; area lights are Mirage's native rectangle light shape; ambient lights (or an optional Preetham sky) set the environment. Spot cone falloff still isn't representable — see Limitations.
- **Output**: 8-bit PNG/JPG/BMP/TGA (view-transform-mapped via `Mirage::ApplyViewTransform`, exposure-scaled first — same transform applied to the interactive Render View preview, so it matches batch output), plus 32-bit float EXR (always raw linear, no view transform/exposure/quantization applied — preserves full render dynamic range).

## Limitations

As of Mirage v1.2.0, point/spot/directional lights are true punctual (delta or near-delta) lights and area lights use Mirage's native rectangle light shape — no more emissive-sphere/far-away-sphere workarounds. What's still approximate or unsupported:

- **Spot cone falloff isn't representable at all** — Mirage's `PunctualLight` has only point/directional types, no cone-angle concept, so a spot light still lights omnidirectionally like a point light (just as a real delta/soft-point light now, not a fake emissive sphere).
- Point/spot soft-shadow radius and the Preetham sky's sun angular diameter are both fixed, reasonable defaults (not derived from any Maya attribute, since none exists) rather than user-adjustable per-light — see `LightTranslator.cpp`.
- Use the **Light Intensity Scale** setting to calibrate brightness; there is no native physical-unit conversion.
- The Preetham sky's sun direction always comes from whichever directional light exists in the scene (first one encountered, if more than one) — there's no independent sun-direction control, so a scene with no directional light gets a fixed default sun angle.
- Physical camera parameters only affect depth-of-field aperture sizing, not field of view: Mirage's `Camera::EffectiveFov()` derives FOV from focal length/sensor height alone, ignoring Film Fit mode and render aspect ratio, so this plugin deliberately keeps computing FOV the way it already did (via `MFnCamera::getRenderingFrustum()`) rather than handing that over.
- Shutter speed/ISO-based physical exposure (`Camera::ComputeExposureMultiplier()`) isn't wired up — no natural Maya camera attribute maps onto them.

Other known gaps:

- Opacity is stochastic cutout (a surface is either fully there or fully not, per sample), not alpha blending — there's no partial-coverage/soft-edge transparency.
- Normal mapping is tangent-space only and requires the mesh to have UVs; object-space normal maps and classic height-field bump aren't supported.
- No Viewport 2.0 / interactive-viewport IPR — interactivity is via the classic, progressive Render View only.
- MASH/particle-instancer translation is single-prototype, single-material only: an `instancer` node referencing more than one prototype object (or a multi-shading-group prototype mesh) only translates its first mesh prototype's geometry and first-found material, with a once-per-instancer warning — matches Mirage's own `PointInstancer` v1 scope (see `PointInstancer.h`). Instancer prototype objects are also not hidden automatically by this plugin the way Maya's own viewport does, so a visible (non-hidden) prototype renders both as itself and as every instanced copy.
- Per-instance shading-group overrides on an instanced shape aren't supported — shading is queried once and applied to all instances of that shape.
- Deforming motion blur doesn't deform shading normals (still barycentric-interpolated from the static, start-of-shutter normals regardless of ray time) — a Mirage v1 scope limit on hit-position/silhouette motion, not shading quality.
- UDIM atlas mip levels can bleed neighboring tiles' edge texels together at coarse mip levels — the atlas is box-filtered as one image with no tile-boundary awareness (matches Mirage's own reference UDIM implementation).
- Area lights (now `Primitive::eRect`) assume no shear in the light's world transform — a sheared area light's shape will render slightly wrong, since `Primitive::startTransform` only supports rotation + uniform scale, not shear (same representational limit as sheared mesh instances, see Instancing above).
- Mirage v1.3.0's "Tier B" named/arbitrary AOVs (`Mirage::NamedAov`, CPU-backend-only, a fixed recognized-name set like `"P"`/`"uv"`) aren't exposed here — there's no natural mapping onto the Render View's fixed AOV channel selector, and they're aimed at compositing pipelines this plugin doesn't otherwise integrate with.
- Mirage v1.3.0's `RenderSettings`/`RenderProduct`/`RenderPass` multi-product pipeline (multiple named outputs — e.g. beauty plus per-AOV files — from one render invocation) isn't wired up either — Maya's own batch renderer is already one output file per frame via Common Render Settings, so this plugin has no multi-invocation problem for it to solve. A batch-mode multi-layer EXR (beauty + AOVs in one file, mirroring Mirage's own `WriteMultiLayerExr`) is a plausible future addition, not implemented here.

## Architecture

- `RenderProcedure` (`MPxCommand`) parses the classic-renderer invocation and drives scene translation; it is destroyed by Maya immediately after `doIt()` returns (non-undoable commands aren't retained), so nothing that needs to outlive a single call lives here.
- `RenderWorker` is a long-lived singleton owning the `RenderSession` (Mirage `Scene` + active `Renderer` backend) and the background render thread — it has to be a singleton independent of `RenderProcedure`'s lifetime for exactly the reason above.
- `RenderSession` handles CPU/GPU backend selection (with automatic fallback) and recreate-gating (Mirage has no scene-dirty-tracking of its own; structural changes require rebuilding the renderer).
- `translators/` (`SceneTranslator`, `MeshTranslator`, `MaterialTranslator`, `LightTranslator`, `InstancerTranslator`, `UdimAtlas`, `MayaTransformUtils`) walk the DAG once per render and translate meshes, materials, lights, and particle/MASH instancers into the scene. `UdimAtlas` is a standalone helper (not a DAG-node translator) that `MaterialTranslator` calls into to composite a UDIM tile set into one atlas texture.
- `ImageWriter` handles 8-bit (PNG/JPG/BMP/TGA) and 32-bit float (EXR) file output for batch rendering.
