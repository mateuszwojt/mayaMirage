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
// that fixed set gets translated. Two things are explicitly untranslatable
// and are flagged (once per scene, not per-material, to avoid log spam)
// rather than silently ignored: opacity/cutout transparency (no field on
// Mirage::Material at all) and bump/normal mapping (Material::bump/bumpTile
// exist but whether they're actually wired into shading is unverified in
// the current renderer - not worth building a translation path onto an
// unconfirmed feature).
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
	bool m_warnedAboutOpacity = false;
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

	void WarnAboutOpacityOnce();
};
