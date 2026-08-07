#pragma once

#include <string>
#include <vector>

#include <mirage/math/Color.h>

// 8-bit PNG/JPG/BMP/TGA (via the vendored stb_image_write.h, also used by
// mirage/tools/scene_renderer) plus, as of Mirage v1.1.0, 32-bit float EXR
// (via the vendored tinyexr.h - same vendoring, same reference tool that
// Mirage's own scene_renderer uses for its own EXR output). Maya's own
// Common Render Settings "Image Format" dropdown offers many formats Mirage
// still categorically can't produce (DPX, Cineon, ...) - callers must not
// pass those through to getRenderOptions()-adjacent format selection, and
// this plugin exposes its own, separately-constrained outputImageFormat
// render-globals attribute rather than trying to interpret Maya's shared
// numeric imageFormat field.
enum class ImageOutputFormat
{
	ePng,
	eJpg,
	eBmp,
	eTga,
	eExr,
};

class ImageWriter
{
public:
	static const char *FormatExtension(ImageOutputFormat format);

	// PNG/JPG/BMP/TGA: tonemaps `pixels` (already fully resolved to linear
	// radiance directly by the renderer backend - both CPU and GPU
	// Render() calls return final, displayable color, no further
	// resolve/division needed by the caller) via Mirage::ToneMap, exactly matching
	// mirage/tools/scene_renderer/SceneRenderer.cpp's own
	// tonemap-then-8-bit-clamp sequence, and writes the result to `path`.
	// EXR: skips tonemapping/quantization entirely and writes `pixels`
	// as-is (raw linear float32 RGBA, alpha forced to 1.0 - see WriteImage's
	// own comment) - the whole point of the format is preserving full
	// dynamic range, matching scene_renderer's WriteExr().
	// Returns false (and logs via MGlobal::displayError) on failure -
	// never throws.
	static bool WriteImage(const std::string &path, const std::vector<Mirage::Color> &pixels,
							int width, int height, ImageOutputFormat format);
};
