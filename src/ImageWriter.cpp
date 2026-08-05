#include "ImageWriter.h"

#include <maya/MGlobal.h>

#include <mirage/utils/Util.h>
#include <mirage/utils/MathUtils.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../third_party/stb_image_write.h"

// tinyexr for 32-bit float EXR output (Mirage v1.1.0) - vendored as
// tinyexr.h plus its two small companion headers (exr_reader.hh,
// streamreader.hh - both self-contained, standard-library-only) from
// upstream syoyo/tinyexr, exactly the same copy
// mirage/tools/scene_renderer/SceneRenderer.cpp already vendors and uses
// for its own EXR output - see that file's WriteExr() for the reference
// implementation this mirrors.
#define TINYEXR_USE_MINIZ 0
#define TINYEXR_USE_STB_ZLIB 1
#define TINYEXR_IMPLEMENTATION
#include "../third_party/tinyexr.h"

const char *ImageWriter::FormatExtension(ImageOutputFormat format)
{
	switch (format)
	{
	case ImageOutputFormat::ePng:
		return "png";
	case ImageOutputFormat::eJpg:
		return "jpg";
	case ImageOutputFormat::eBmp:
		return "bmp";
	case ImageOutputFormat::eTga:
		return "tga";
	case ImageOutputFormat::eExr:
		return "exr";
	}
	return "png";
}

namespace
{
	// Raw (untonemapped, unquantized) float32 RGBA EXR output - preserves
	// the renderer's full dynamic range, unlike every other format this
	// class writes. Mirrors mirage/tools/scene_renderer/SceneRenderer.cpp's
	// own WriteExr() exactly, including writing a fully-opaque alpha
	// channel rather than passing `pixel.w` through: `pixels[i].w` here is
	// whatever raw accumulated-sample-weight value the CPU/GPU backend left
	// in it (see RenderWorker::ResolveBackendPixel), not a real [0,1]
	// coverage value.
	bool WriteExr(const std::string &path, const std::vector<Mirage::Color> &pixels, int width, int height)
	{
		std::vector<float> rgba(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
		for (int i = 0; i < width * height; ++i)
		{
			rgba[i * 4 + 0] = pixels[i].x;
			rgba[i * 4 + 1] = pixels[i].y;
			rgba[i * 4 + 2] = pixels[i].z;
			rgba[i * 4 + 3] = 1.0f;
		}

		const char *err = nullptr;
		const int ret = SaveEXR(rgba.data(), width, height, 4, /*save_as_fp16=*/0, path.c_str(), &err);
		if (ret != TINYEXR_SUCCESS)
		{
			MGlobal::displayError(MString("Mirage: failed to save EXR '") + path.c_str() + "': " +
								   (err ? err : "unknown error"));
			if (err)
				FreeEXRErrorMessage(err);
			return false;
		}
		return true;
	}
}

bool ImageWriter::WriteImage(const std::string &path, const std::vector<Mirage::Color> &pixels,
							  int width, int height, ImageOutputFormat format)
{
	if (static_cast<int>(pixels.size()) != width * height)
	{
		MGlobal::displayError("Mirage: ImageWriter::WriteImage - pixel buffer size doesn't match width*height.");
		return false;
	}

	if (format == ImageOutputFormat::eExr)
		return WriteExr(path, pixels, width, height);

	// Matches mirage/tools/scene_renderer/SceneRenderer.cpp's own
	// tonemap-then-8-bit-clamp sequence exactly, for output consistency
	// with the rest of the Mirage tool family. Note ToneMap's second
	// parameter is named "limit" and is actually unused by its
	// implementation (mirage/utils/Util.h) - exposure is not applied here,
	// matching that reference tool's existing (if perhaps incomplete)
	// behavior rather than silently diverging from it.
	std::vector<unsigned char> imageData(static_cast<size_t>(width) * height * 3);
	for (int i = 0; i < width * height; ++i)
	{
		Mirage::Color pixel = Mirage::ToneMap(pixels[i], 0.0f);

		imageData[i * 3 + 0] = static_cast<unsigned char>(255.0f * Mirage::Clamp(pixel.x, 0.0f, 1.0f));
		imageData[i * 3 + 1] = static_cast<unsigned char>(255.0f * Mirage::Clamp(pixel.y, 0.0f, 1.0f));
		imageData[i * 3 + 2] = static_cast<unsigned char>(255.0f * Mirage::Clamp(pixel.z, 0.0f, 1.0f));
	}

	bool success = false;
	switch (format)
	{
	case ImageOutputFormat::ePng:
		success = stbi_write_png(path.c_str(), width, height, 3, imageData.data(), width * 3) != 0;
		break;
	case ImageOutputFormat::eJpg:
		success = stbi_write_jpg(path.c_str(), width, height, 3, imageData.data(), 95) != 0;
		break;
	case ImageOutputFormat::eBmp:
		success = stbi_write_bmp(path.c_str(), width, height, 3, imageData.data()) != 0;
		break;
	case ImageOutputFormat::eTga:
		success = stbi_write_tga(path.c_str(), width, height, 3, imageData.data()) != 0;
		break;
	case ImageOutputFormat::eExr:
		break; // handled by the early return above
	}

	if (!success)
	{
		MGlobal::displayError(MString("Mirage: failed to write image '") + path.c_str() + "'.");
	}

	return success;
}
