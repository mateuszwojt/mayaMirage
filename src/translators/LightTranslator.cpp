#include "LightTranslator.h"

#include <iostream>

#include <maya/MFn.h>
#include <maya/MFnPointLight.h>
#include <maya/MFnSpotLight.h>
#include <maya/MFnDirectionalLight.h>
#include <maya/MFnAreaLight.h>
#include <maya/MFnAmbientLight.h>
#include <maya/MTransformationMatrix.h>
#include <maya/MMatrix.h>
#include <maya/MQuaternion.h>
#include <maya/MColor.h>
#include <maya/MFloatVector.h>
#include <maya/MGlobal.h>
#include <maya/MStatus.h>
#include <maya/MString.h>

#include <mirage/lights/PunctualLight.h>
#include <mirage/lights/Skylight.h>
#include <mirage/utils/MathUtils.h>
#include <mirage/utils/Util.h>

void LightTranslator::Translate(const MDagPath &lightPath)
{
	MObject obj = lightPath.node();

	// Order matters: MFnSpotLight/MFnAreaLight/MFnAmbientLight/
	// MFnDirectionalLight/MFnPointLight are a real inheritance chain in
	// Maya (e.g. a spot light also reports true for kPointLight-ish base
	// checks in some API surfaces) - apiType() gives the exact leaf type,
	// so dispatch on that rather than hasFn() to avoid a spot light being
	// mis-handled as a point light.
	switch (obj.apiType())
	{
	case MFn::kSpotLight:
		TranslateSpotLight(lightPath);
		break;
	case MFn::kAreaLight:
		TranslateAreaLight(lightPath);
		break;
	case MFn::kAmbientLight:
		TranslateAmbientLight(lightPath);
		break;
	case MFn::kDirectionalLight:
		TranslateDirectionalLight(lightPath);
		break;
	case MFn::kPointLight:
		TranslatePointLight(lightPath);
		break;
	default:
		break;
	}
}

void LightTranslator::TranslatePointLight(const MDagPath &path)
{
	MFnPointLight light(path);
	MColor color = light.color();
	float intensity = light.intensity();

	MTransformationMatrix xform(path.inclusiveMatrix());
	MVector pos = xform.getTranslation(MSpace::kTransform);

	// A small, fixed soft-shadow radius - Maya's point light is a true
	// delta light with no physical size of its own to derive one from, so
	// this is an arbitrary-but-reasonable modeling choice (same value the
	// pre-1.2.0 emissive-sphere approximation used), now driving Mirage's
	// real PunctualLight soft-shadow sampling (PunctualLightSample's
	// radius > 0 jitter) instead of faking it via sphere surface area.
	const float radius = 0.05f;
	AddPointLight(Mirage::Vec3(static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(pos.z)),
				  Mirage::Vec3(color.r, color.g, color.b), intensity, radius);
}

void LightTranslator::TranslateSpotLight(const MDagPath &path)
{
	MFnSpotLight light(path);
	MColor color = light.color();
	float intensity = light.intensity();

	MTransformationMatrix xform(path.inclusiveMatrix());
	MVector pos = xform.getTranslation(MSpace::kTransform);

	// Mirage's PunctualLightType is only ePoint/eDirectional - there is
	// still no light type carrying a direction/cone-angle concept, so a
	// spot's cone falloff remains unrepresentable, same limitation as
	// before v1.2.0. This now maps to an ordinary ePoint punctual light
	// (dropping the old "offset an emissive sphere along the spot's
	// direction" hack, which only vaguely biased where the light visually
	// originated from) - a strictly better approximation than before (a
	// true delta/soft-point light instead of a fake emissive sphere), just
	// still omnidirectional: geometry outside where the real cone would be
	// is still lit.
	const float radius = 0.05f;
	AddPointLight(Mirage::Vec3(static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(pos.z)),
				  Mirage::Vec3(color.r, color.g, color.b), intensity, radius);
}

void LightTranslator::TranslateDirectionalLight(const MDagPath &path)
{
	MFnDirectionalLight light(path);
	MColor color = light.color();
	float intensity = light.intensity();

	// MFnLight::lightDirection()'s no-instance/no-space overload
	// (the pre-v1.2.0 code's original call here) comes back as a zero
	// vector when called on an MFnDirectionalLight built directly from a
	// DAG path outside an actual shading callback - it silently went
	// unnoticed before because the old "huge emissive sphere placed at
	// lightPos - dir*distance" approximation degraded gracefully with
	// dir=(0,0,0) (the sphere just landed at the light's own position,
	// still large enough to light the scene); a true delta PunctualLight
	// has no such tolerance - Normalize((0,0,0)) is degenerate, so the
	// light silently contributed nothing at all. The explicit-instance/
	// explicit-space overload below is Maya's documented API for this and
	// returns the correct world-space direction.
	MStatus status;
	MFloatVector dir = light.lightDirection(0, MSpace::kWorld, &status);
	Mirage::Vec3 direction(dir.x, dir.y, dir.z);

	if (status != MS::kSuccess || Mirage::LengthSq(direction) < 1.e-12f)
	{
		MGlobal::displayWarning(MString("Mirage: directional light '") + path.partialPathName()
								 + "' resolved to a degenerate direction - falling back to straight down.");
		direction = Mirage::Vec3(0.0f, -1.0f, 0.0f);
	}

	// True parallel-ray delta light via Mirage v1.2.0's eDirectional
	// PunctualLight - this replaces the old "large emissive sphere placed
	// far away" workaround entirely (previously documented as the weakest
	// of the light approximations: an arbitrary fixed distance that
	// distorted shadow softness). `angle` (angular diameter, e.g. the
	// sun's ~0.53 degrees) is left at 0 for a sharp delta shadow - there's
	// no Maya attribute to derive a physical value from, same
	// "don't invent precision" choice as the point/spot radius above being
	// a fixed constant rather than a derived one.
	Mirage::PunctualLight pLight;
	pLight.type = Mirage::PunctualLightType::eDirectional;
	pLight.direction = direction;
	pLight.color = Mirage::Vec3(color.r, color.g, color.b);
	pLight.intensity = intensity * m_intensityScale;
	m_scene.lights.push_back(pLight);

	std::cout << "\tDirectional light: direction (" << direction.x << ", " << direction.y << ", " << direction.z
			   << "), color (" << color.r << ", " << color.g << ", " << color.b << "), intensity " << intensity
			   << " -> Mirage intensity " << pLight.intensity << " (scale " << m_intensityScale << ")" << std::endl;

	// Remembered for FinalizeSky()'s Preetham bake - see its comment and
	// RenderGlobals.h's SkySettings for why the sun direction comes from
	// here rather than a dedicated attribute. First directional light
	// encountered wins, same "last/first one wins" simplification already
	// used for multiple ambient lights below.
	if (!m_sawDirectionalLight)
	{
		m_lastDirectionalLightDir = direction;
		m_sawDirectionalLight = true;
	}
}

void LightTranslator::TranslateAreaLight(const MDagPath &path)
{
	MFnAreaLight light(path);
	MColor color = light.color();
	float intensity = light.intensity();

	// Maya's area light is a unit (1x1) quad in the light shape's own local
	// XY plane (normal = local +Z) - exactly Primitive::eRect's local-space
	// convention (see Primitive.h's RectGeometry comment), so decomposing
	// the world matrix into translation/rotation/scale and feeding
	// scaleX/scaleY straight into rect.width/height is a direct mapping,
	// no axis remapping needed. This replaces the old two-triangle emissive
	// Mesh built by baking world-space corners directly - simpler, and uses
	// the shape Mirage v1.2.0 added specifically for this. The tradeoff:
	// MTransformationMatrix's decomposition assumes no shear, unlike the
	// old bake-the-corners approach which was shear-correct "for free" -
	// sheared area lights (rare in practice) will render slightly wrong
	// now, same kind of tradeoff MeshTranslator already documents for
	// sheared mesh instances.
	MTransformationMatrix xform(path.inclusiveMatrix());
	MVector t = xform.getTranslation(MSpace::kWorld);
	MQuaternion rotation = xform.rotation();
	double scale[3] = {1.0, 1.0, 1.0};
	xform.getScale(scale, MSpace::kWorld);

	Mirage::Primitive primitive;
	primitive.type = Mirage::eRect;
	primitive.rect.width = static_cast<float>(scale[0]);
	primitive.rect.height = static_cast<float>(scale[1]);
	primitive.startTransform = Mirage::Transform(
		Mirage::Vec3(static_cast<float>(t.x), static_cast<float>(t.y), static_cast<float>(t.z)),
		Mirage::Quat(static_cast<float>(rotation.x), static_cast<float>(rotation.y), static_cast<float>(rotation.z), static_cast<float>(rotation.w)),
		1.0f);
	primitive.endTransform = primitive.startTransform;
	primitive.lightSamples = 1;

	auto material = std::make_unique<Mirage::Material>();
	material->color = Mirage::Vec3(0.0f);
	material->emission = Mirage::Vec3(color.r, color.g, color.b) * intensity * m_intensityScale;
	primitive.materialIndex = m_scene.AddMaterial(std::move(material));

	m_scene.AddPrimitive(primitive);
}

void LightTranslator::TranslateAmbientLight(const MDagPath &path)
{
	MFnAmbientLight light(path);
	MColor color = light.color();
	float intensity = light.intensity();

	// A flat, uniform-from-everywhere contribution is a good match for
	// Mirage's gradient Sky - setting both horizon and zenith to the same
	// color makes it flat rather than gradiented. If multiple ambient
	// lights exist, the last one encountered wins. Superseded by a
	// Preetham bake in FinalizeSky() if that's enabled - see its comment.
	Mirage::Vec3 c = Mirage::Vec3(color.r, color.g, color.b) * intensity * m_intensityScale;
	m_scene.sky.horizon = c;
	m_scene.sky.zenith = c;
	m_sawAmbientLight = true;

	std::cout << "\tAmbient light: color (" << color.r << ", " << color.g << ", " << color.b << "), intensity "
			   << intensity << " -> sky (" << c.x << ", " << c.y << ", " << c.z << ")" << std::endl;
}

void LightTranslator::FinalizeSky(bool preetham, float turbidity)
{
	std::cout << "\tFinalizeSky: preetham=" << (preetham ? "true" : "false") << ", turbidity=" << turbidity
			   << ", sawAmbientLight=" << (m_sawAmbientLight ? "true" : "false")
			   << ", sawDirectionalLight=" << (m_sawDirectionalLight ? "true" : "false")
			   << ", scene.lights.size()=" << m_scene.lights.size() << std::endl;

	if (preetham)
	{
		// Sun direction comes from whichever directional light Translate()
		// saw first (see TranslateDirectionalLight); a scene with none
		// falls back to a fixed, reasonable default angle rather than
		// baking a degenerate/zenith-locked sky.
		const Mirage::Vec3 sunDir = m_sawDirectionalLight ? -m_lastDirectionalLightDir : Mirage::Vec3(0.3f, 0.8f, 0.2f);
		m_scene.sky.probe = Mirage::BakePreethamSky(sunDir, turbidity);
		return;
	}

	if (m_sawAmbientLight)
		return;

	Mirage::Vec3 dim(0.05f, 0.05f, 0.05f);
	m_scene.sky.horizon = dim;
	m_scene.sky.zenith = dim;
}

void LightTranslator::AddPointLight(const Mirage::Vec3 &position, const Mirage::Vec3 &color, float intensity, float radius)
{
	Mirage::PunctualLight pLight;
	pLight.type = Mirage::PunctualLightType::ePoint;
	pLight.position = position;
	pLight.color = color;
	pLight.intensity = intensity * m_intensityScale;
	pLight.radius = radius;
	m_scene.lights.push_back(pLight);

	std::cout << "\tPoint/spot light: position (" << position.x << ", " << position.y << ", " << position.z
			   << "), color (" << color.x << ", " << color.y << ", " << color.z << "), intensity " << intensity
			   << " -> Mirage intensity " << pLight.intensity << " (scale " << m_intensityScale << ")" << std::endl;
}
