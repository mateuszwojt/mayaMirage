#pragma once

#include <maya/MDagPath.h>
#include <maya/MPoint.h>

#include <mirage/core/Scene.h>

// Maps Maya light nodes onto Mirage's actual light model, which is just two
// things: Scene::sky (a flat horizon/zenith gradient, or an HDR probe) and
// ordinary emissive Primitives with lightSamples > 0 (explicitly
// NEE-sampled - see mirage/core/Renderer.cpp's SampleLights(), which just
// scans scene.primitives for that flag; there is no dedicated
// point/spot/directional/area light type in Mirage at all). Every mapping
// below except area lights is therefore a physically-approximate stand-in,
// not a faithful translation - see each Translate*() method's comment for
// how faithful/approximate it specifically is.
class LightTranslator
{
public:
	LightTranslator(Mirage::Scene &scene, float intensityScale)
		: m_scene(scene), m_intensityScale(intensityScale) {}

	// Dispatches by light type (point/spot/directional/area/ambient). No-op
	// for any other node type - callers should already have filtered to
	// light shape nodes.
	void Translate(const MDagPath &lightPath);

	// Call once after every light in the scene has been passed to
	// Translate(). If no MFnAmbientLight was ever encountered, sets a dim
	// neutral default sky so the scene isn't rendered pure black - an
	// honest fallback, not a translation of anything the user actually
	// authored (unlike the previous version of this plugin, which always
	// hardcoded a bright, saturated blue sky regardless of the scene's
	// actual light content).
	void FinalizeSky();

private:
	Mirage::Scene &m_scene;
	float m_intensityScale;
	bool m_sawAmbientLight = false;

	void TranslatePointLight(const MDagPath &path);
	void TranslateSpotLight(const MDagPath &path);
	void TranslateDirectionalLight(const MDagPath &path);
	void TranslateAreaLight(const MDagPath &path);
	void TranslateAmbientLight(const MDagPath &path);

	// Adds an emissive sphere primitive at `position` with `radius`,
	// emitting `emission` (already color/intensity/scale-combined),
	// explicitly NEE-sampled.
	void AddEmissiveSphere(const Mirage::Vec3 &position, float radius, const Mirage::Vec3 &emission);

	// Adds an emissive quad (a two-triangle Mesh/Primitive) with the given
	// four world-space corners, emitting `emission`.
	void AddEmissiveQuad(const MPoint corners[4], const Mirage::Vec3 &emission);
};
