#pragma once

#include <string>
#include <vector>

#include <mirage/math/Color.h>

// Only 8-bit PNG/JPG/BMP/TGA are supported - the only image writer anywhere
// in the Mirage codebase family is the vendored stb_image_write.h (also
// used by mirage/tools/scene_renderer), which doesn't do EXR or any other
// 32-bit-float format. Maya's own Common Render Settings "Image Format"
// dropdown offers many formats Mirage categorically can't produce (EXR,
// DPX, Cineon, ...) - callers must not pass those through to
// getRenderOptions()-adjacent format selection, and this plugin exposes its
// own, separately-constrained outputImageFormat render-globals attribute
// rather than trying to interpret Maya's shared numeric imageFormat field.
enum class ImageOutputFormat
{
	ePng,
	eJpg,
	eBmp,
	eTga,
};

class ImageWriter
{
public:
	static const char *FormatExtension(ImageOutputFormat format);

	// Tonemaps `pixels` (already backend-resolved to linear radiance - see
	// RenderWorker's ResolveBackendPixel, which must be applied before
	// calling this) via Mirage::ToneMap, exactly matching
	// mirage/tools/scene_renderer/SceneRenderer.cpp's own
	// tonemap-then-8-bit-clamp sequence, and writes the result to `path`.
	// Returns false (and logs via MGlobal::displayError) on failure -
	// never throws.
	static bool WriteImage(const std::string &path, const std::vector<Mirage::Color> &pixels,
							int width, int height, ImageOutputFormat format);
};
