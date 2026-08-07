#pragma once

#include <maya/MDagPath.h>

#include <mirage/core/Scene.h>

// Maps Maya light nodes onto Mirage's light model. As of Mirage v1.2.0 that
// model is three things, not two: Scene::lights (true delta-distribution
// PunctualLight point/directional lights - see mirage/lights/PunctualLight.h),
// ordinary emissive Primitives with lightSamples > 0 (area lights only, now
// via the dedicated Primitive::eRect shape - see mirage/prims/Primitive.h),
// and Scene::sky (a flat horizon/zenith gradient, or an analytic Preetham
// probe - see mirage/lights/Skylight.h). There is still no dedicated spot
// light type at all - a spot's cone falloff remains unrepresentable and
// falls back to an omnidirectional point light, same as before.
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
	// Translate(). Precedence for what ends up in Scene::sky: a Preetham
	// bake (if `preetham` is true) beats an authored MFnAmbientLight color,
	// which beats the dim neutral default (so the scene isn't rendered pure
	// black when nothing at all was authored) - an honest fallback chain,
	// not a translation of anything the user actually authored beyond what
	// each tier's name implies. The Preetham bake's sun direction is
	// whichever directional light Translate() saw first, or a fixed default
	// angle if the scene has none - see RenderGlobals.h's SkySettings
	// comment for why there's no separate direction control.
	void FinalizeSky(bool preetham, float turbidity);

private:
	Mirage::Scene &m_scene;
	float m_intensityScale;
	bool m_sawAmbientLight = false;
	bool m_sawDirectionalLight = false;
	Mirage::Vec3 m_lastDirectionalLightDir = Mirage::Vec3(0.0f, -1.0f, 0.0f);

	void TranslatePointLight(const MDagPath &path);
	void TranslateSpotLight(const MDagPath &path);
	void TranslateDirectionalLight(const MDagPath &path);
	void TranslateAreaLight(const MDagPath &path);
	void TranslateAmbientLight(const MDagPath &path);

	// Adds an ePoint PunctualLight (used by both point and spot lights - a
	// spot's cone direction/angle has nowhere to go, see the class comment)
	// at `position`, emitting `color * intensity * m_intensityScale`, with
	// `radius` as the soft-shadow sample radius (0 = a true delta light).
	void AddPointLight(const Mirage::Vec3 &position, const Mirage::Vec3 &color, float intensity, float radius);
};
