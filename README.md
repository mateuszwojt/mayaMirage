# Mirage Maya Plugin

A Maya integration for the [Mirage](https://github.com/mateuszwojt/Mirage) path tracer, targeting **Maya 2026/2027** on **macOS (Apple Silicon)** and Mirage's Slang/Vulkan (and CPU) renderer backends.

The plugin registers Mirage as a classic Maya renderer (`Render > Render Current Frame`, `Render Settings`, and batch rendering via `Render`/`mayabatch`), with a progressive, cancellable Render View, real Maya scene translation (materials, lights, instancing, motion blur), AOVs, and file output.

## Requirements

- Maya 2026 or 2027, with the standalone Maya devkit package extracted somewhere on disk (Maya 2023+ no longer bundles the devkit inside the application install).
- macOS on Apple Silicon — Mirage's GPU backend (Slang shaders via slang-rhi/MoltenVK) only builds and runs on macOS.
- A built and installed [`mirage`](../mirage) (the `find_package(Mirage CONFIG)` package under `mirage/install/`).

## Building

```sh
export DEVKIT_LOCATION=/path/to/devkitBase   # contains include/ and lib/ directly

cmake -S . -B build \
    -DMirage_DIR=/path/to/mirage/install/lib/cmake/Mirage \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

`DEVKIT_LOCATION` (or the `MAYA_DEVKIT_ROOT` CMake cache variable) points at the extracted Maya devkit; `Mirage_DIR` points at Mirage's installed CMake package. Installing (`cmake --install build`) copies the built plugin and `scripts/MirageMaya` into Maya's per-user plug-ins/scripts directories.

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
- **Instancing**: Maya DAG instances whose world transform is rigid (translate/rotate/uniform-scale) share one object-space mesh and BVH across all instances — genuine GPU-memory-shared instancing, not per-instance geometry copies. Instances with shear or non-uniform scale (which Mirage's transform can't represent) fall back to a baked private copy.
- **Motion blur**: rigid-transform only — two time samples (at the configured shutter offsets) feed Mirage's per-primitive transform interpolation. Deforming/skinned meshes are not supported (Mirage's mesh format has no per-vertex motion representation).
- **Materials**: `standardSurface` (primary target — base color, metalness, roughness, specular, coat, sheen, transmission, subsurface, emission, IOR) plus legacy Lambert/Blinn/Phong. Connected `file` textures are supported for base color/roughness/metalness.
- **Lights**: point, spot, directional, area, and ambient. Mirage has no native point/spot/directional light type — see Limitations.

## Limitations

Mirage's own light model is just a sky gradient plus emissive geometry — there's no dedicated point/spot/directional/area light type. Translated lights are therefore physically approximate to varying degrees:

- **Area lights** are the best match (Mirage's model _is_ emissive geometry).
- **Point/spot lights** become small emissive spheres (a standard path-tracer workaround for a zero-area source); spot cone falloff isn't representable at all.
- **Directional lights** are the weakest approximation — a large emissive sphere placed far away, which doesn't reproduce correct shadow softness.
- Use the **Light Intensity Scale** setting to calibrate brightness; there is no native physical-unit conversion.

Other known gaps:

- No opacity/cutout transparency — `Mirage::Material` has no such field; non-opaque materials render fully opaque (a warning is logged once per scene).
- No bump/normal mapping — unverified whether it's wired into Mirage's shading at all.
- No EXR/32-bit-float output — only 8-bit PNG/JPG/BMP/TGA (the only image writer in the Mirage codebase family).
- No Viewport 2.0 / interactive-viewport IPR — interactivity is via the classic, progressive Render View only.
- No MASH/particle-instancer support, only regular DAG instancing.
- Per-instance shading-group overrides on an instanced shape aren't supported — shading is queried once and applied to all instances of that shape.

## Architecture

- `RenderProcedure` (`MPxCommand`) parses the classic-renderer invocation and drives scene translation; it is destroyed by Maya immediately after `doIt()` returns (non-undoable commands aren't retained), so nothing that needs to outlive a single call lives here.
- `RenderWorker` is a long-lived singleton owning the `RenderSession` (Mirage `Scene` + active `Renderer` backend) and the background render thread — it has to be a singleton independent of `RenderProcedure`'s lifetime for exactly the reason above.
- `RenderSession` handles CPU/GPU backend selection (with automatic fallback) and recreate-gating (Mirage has no scene-dirty-tracking of its own; structural changes require rebuilding the renderer).
- `translators/` (`SceneTranslator`, `MeshTranslator`, `MaterialTranslator`, `LightTranslator`, `MayaTransformUtils`) walk the DAG once per render and translate meshes, materials, and lights into the scene.
- `ImageWriter` handles 8-bit file output for batch rendering.
