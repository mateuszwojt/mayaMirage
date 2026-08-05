#pragma once

#include <string>
#include <unordered_map>

#include <maya/MObject.h>
#include <maya/MPlug.h>

#include <mirage/core/Scene.h>
#include <mirage/shaders/Material.h>
#include <mirage/shaders/TextureLoader.h>

// Translates Maya shading-engine (shading group) nodes into Mirage::Material
// entries registered in a Scene, deduplicated by shading-engine name so the
// same shading group referenced by many meshes is only translated once.
//
// Mirage::Material is a fixed Disney-principled-BSDF field set with no
// mechanism for host-supplied/custom shading graphs - only what maps onto
// that fixed set gets translated. As of Mirage v1.1.0 both opacity/cutout
// transparency (Material::opacity/opacityTextureIndex) and tangent-space
// normal mapping (Material::normalTextureIndex, fed via a standard
// file->bump2d->normalCamera network) have real fields and are translated
// below - previously both were untranslatable and only flagged with a
// once-per-scene warning.
class MaterialTranslator
{
public:
	explicit MaterialTranslator(Mirage::Scene &scene) : m_scene(scene) {}

	// Resolves `shadingEngine` (a Maya "shadingEngine"-type node, e.g.
	// lambert1SG) to a Scene material index, translating its connected
	// surface shader and registering it on first use. Pass MObject::kNullObj
	// for faces with no shading engine assigned at all - returns the shared
	// placeholder-material index in that case.
	int TranslateShadingEngine(const MObject &shadingEngine);

private:
	Mirage::Scene &m_scene;
	// Keyed by shading-engine name (or the placeholder key for an
	// unassigned face) - avoids re-translating (and re-loading any
	// connected textures for) the same shading engine every time another
	// mesh references it. Scene::FindOrAddMaterial already dedupes by the
	// same key on its own side, but only after the (potentially expensive)
	// Material has already been built, textures decoded and all.
	std::unordered_map<std::string, int> m_cache;

	std::unique_ptr<Mirage::Material> TranslateSurfaceShader(const MObject &shaderNode);
	std::unique_ptr<Mirage::Material> TranslateStandardSurface(const MObject &shaderNode);
	std::unique_ptr<Mirage::Material> TranslateLegacyShader(const MObject &shaderNode);

	// Resolves a plug to a directly-connected `file` texture node, if any,
	// registers it in the scene, and returns its texture index (-1 if the
	// plug isn't texture-connected, or the file couldn't be loaded).
	int ResolveFileTexture(const MPlug &plug, Mirage::TextureColorSpace colorSpace);

	// Resolves a shader's `normalCamera`-style input plug through the
	// standard Maya file -> bump2d -> shader network to a tangent-space
	// normal map texture index. Only bump2d nodes set to "Tangent Space
	// Normals" (bumpInterp == 1) are translated - plain height-field bump
	// (bumpInterp == 0) has no Mirage::Material field to map onto and is
	// left unset, same as an unconnected plug (-1, safe no-op).
	int ResolveNormalMapTexture(const MPlug &plug);
};
