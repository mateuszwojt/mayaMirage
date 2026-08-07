#pragma once

#include <memory>
#include <string>

#include <mirage/shaders/Texture.h>
#include <mirage/shaders/TextureLoader.h>

// Builds one composited Mirage::Texture atlas from a UDIM tile set (Mirage
// v1.2.0's Texture::udimGridWidth/Height + TextureSampling.h's UdimAtlasUV
// remap). Mirage's own reference implementation of this (LoadUdimAtlas) only
// exists as tool-local code in Mirage's tools/scene_renderer/
// SceneRenderer.cpp, not exported by the installed library/headers, so it's
// ported here rather than depended on directly - same algorithm (tile
// discovery, per-tile resolution consistency check, atlas composition,
// GenerateMips() once on the finished atlas), just sourcing the tile
// pattern from a Maya file node's UDIM data (see MaterialTranslator::
// ResolveFileTexture) instead of a scene-file CLI argument.
//
// `patternPath` must contain the literal token "<UDIM>" exactly once
// (already resolved to an absolute path by the caller). Every file in
// patternPath's directory matching "<prefix><digits><suffix>" is treated as
// one tile, keyed by the standard UDIM numbering (1001 + u + 10*v). Returns
// nullptr (logging via MGlobal::displayWarning, same "missing/broken
// texture doesn't fail the whole scene load" convention as
// Mirage::LoadTextureFromFile) if no tiles are found or tiles disagree on
// resolution.
std::unique_ptr<Mirage::Texture> LoadUdimAtlas(const std::string &patternPath, Mirage::TextureColorSpace colorSpace);
