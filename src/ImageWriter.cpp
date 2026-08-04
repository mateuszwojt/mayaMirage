#include "ImageWriter.h"

#include <maya/MGlobal.h>

#include <mirage/utils/Util.h>
#include <mirage/utils/MathUtils.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../third_party/stb_image_write.h"

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
	}
	return "png";
}

bool ImageWriter::WriteImage(const std::string &path, const std::vector<Mirage::Color> &pixels,
							  int width, int height, ImageOutputFormat format)
{
	if (static_cast<int>(pixels.size()) != width * height)
	{
		MGlobal::displayError("Mirage: ImageWriter::WriteImage - pixel buffer size doesn't match width*height.");
		return false;
	}

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
	}

	if (!success)
	{
		MGlobal::displayError(MString("Mirage: failed to write image '") + path.c_str() + "'.");
	}

	return success;
}
