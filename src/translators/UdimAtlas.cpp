#include "UdimAtlas.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

#include <maya/MGlobal.h>
#include <maya/MString.h>

std::unique_ptr<Mirage::Texture> LoadUdimAtlas(const std::string &patternPath, Mirage::TextureColorSpace colorSpace)
{
	const std::string kToken = "<UDIM>";
	size_t tokenPos = patternPath.find(kToken);
	if (tokenPos == std::string::npos)
		return nullptr;

	std::string prefix = patternPath.substr(0, tokenPos);
	std::string suffix = patternPath.substr(tokenPos + kToken.size());

	std::filesystem::path dir = std::filesystem::path(prefix).parent_path();
	std::string filePrefix = std::filesystem::path(prefix).filename().string();
	if (dir.empty())
		dir = ".";

	struct Tile
	{
		int u, v;
		std::unique_ptr<Mirage::Texture> tex;
	};
	std::vector<Tile> tiles;

	std::error_code ec;
	for (const auto &entry : std::filesystem::directory_iterator(dir, ec))
	{
		if (ec || !entry.is_regular_file())
			continue;

		std::string name = entry.path().filename().string();
		if (name.size() <= filePrefix.size() + suffix.size())
			continue;
		if (name.compare(0, filePrefix.size(), filePrefix) != 0)
			continue;
		if (name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0)
			continue;

		std::string digits = name.substr(filePrefix.size(), name.size() - filePrefix.size() - suffix.size());
		if (digits.empty() || !std::all_of(digits.begin(), digits.end(), [](unsigned char c) { return std::isdigit(c); }))
			continue;

		int udim = std::stoi(digits);
		int u = (udim - 1001) % 10;
		int v = (udim - 1001) / 10;
		if (u < 0 || v < 0)
		{
			MGlobal::displayWarning(MString("Mirage: skipping UDIM tile with out-of-range number ") + std::to_string(udim).c_str()
									 + " (" + name.c_str() + ").");
			continue;
		}

		// generateMips=false - mips are generated once for the whole
		// composited atlas below (per-tile mip generation would need to
		// happen twice - once per tile, once for the atlas - to no
		// benefit, since only the atlas's own mip chain is ever sampled).
		auto tex = Mirage::LoadTextureFromFile(entry.path().string(), colorSpace, /*generateMips=*/false);
		if (!tex)
			continue;

		tiles.push_back(Tile{u, v, std::move(tex)});
	}

	if (tiles.empty())
	{
		MGlobal::displayWarning(MString("Mirage: no UDIM tiles found matching pattern '") + patternPath.c_str() + "'.");
		return nullptr;
	}

	int tileW = tiles[0].tex->width;
	int tileH = tiles[0].tex->height;
	int gridW = 0, gridH = 0;
	for (const auto &t : tiles)
	{
		gridW = std::max(gridW, t.u + 1);
		gridH = std::max(gridH, t.v + 1);
		if (t.tex->width != tileW || t.tex->height != tileH)
		{
			MGlobal::displayWarning(MString("Mirage: UDIM tile resolution mismatch for '") + patternPath.c_str()
									 + "' - rejecting UDIM set.");
			return nullptr;
		}
	}

	auto atlas = std::make_unique<Mirage::Texture>();
	atlas->width = gridW * tileW;
	atlas->height = gridH * tileH;
	atlas->depth = 1;
	atlas->udimGridWidth = gridW;
	atlas->udimGridHeight = gridH;
	size_t atlasTexelCount = static_cast<size_t>(atlas->width) * static_cast<size_t>(atlas->height) * 4;
	atlas->data = new float[atlasTexelCount];
	// Unpopulated cells (a sparse UDIM set, e.g. tiles 1001 and 1050 with
	// nothing authored in between) stay black rather than garbage.
	std::fill(atlas->data, atlas->data + atlasTexelCount, 0.0f);

	for (const auto &t : tiles)
	{
		int originX = t.u * tileW;
		int originY = t.v * tileH;
		for (int y = 0; y < tileH; ++y)
		{
			const float *srcRow = t.tex->data + static_cast<size_t>(y) * tileW * 4;
			float *dstRow = atlas->data + (static_cast<size_t>(originY + y) * atlas->width + originX) * 4;
			std::copy(srcRow, srcRow + static_cast<size_t>(tileW) * 4, dstRow);
		}
	}

	// Known artifact, accepted (matches Mirage's own scene_renderer
	// reference implementation): box-filtering across the whole atlas
	// doesn't know about tile boundaries, so coarse mip levels visibly
	// bleed neighboring tiles' edge texels together. A per-tile-padded
	// atlas layout would fix this but is out of scope here.
	atlas->GenerateMips();
	return atlas;
}
