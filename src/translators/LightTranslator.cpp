#include "LightTranslator.h"

#include <algorithm>
#include <cmath>

#include <maya/MFn.h>
#include <maya/MFnPointLight.h>
#include <maya/MFnSpotLight.h>
#include <maya/MFnDirectionalLight.h>
#include <maya/MFnAreaLight.h>
#include <maya/MFnAmbientLight.h>
#include <maya/MTransformationMatrix.h>
#include <maya/MMatrix.h>
#include <maya/MColor.h>
#include <maya/MFloatVector.h>

#include <mirage/utils/MathUtils.h>
#include <mirage/utils/Util.h>

namespace
{
	// Emissive-sphere point/spot-light approximations use an arbitrary,
	// fixed radius (Maya's point/spot lights are true delta lights with no
	// physical size of their own to derive one from). Scaling emission
	// inversely with that sphere's surface area keeps total emitted power
	// roughly consistent regardless of this arbitrary choice, rather than
	// having brightness be an accidental artifact of it - a standard
	// technique for "light with radius" approximations in offline renderers.
	float SphereAreaCompensation(float radius)
	{
		const float area = 4.0f * static_cast<float>(M_PI) * radius * radius;
		return 1.0f / std::max(area, 1e-6f);
	}
}

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

	// Mirage has no true point/delta light - approximate with a small
	// emissive sphere (the standard path-tracer workaround for a
	// physically-zero-area source). The radius is an arbitrary modeling
	// choice, not derived from any Maya attribute.
	const float radius = 0.05f;
	Mirage::Vec3 emission = Mirage::Vec3(color.r, color.g, color.b) * intensity * m_intensityScale * SphereAreaCompensation(radius);

	AddEmissiveSphere(Mirage::Vec3(static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(pos.z)), radius, emission);
}

void LightTranslator::TranslateSpotLight(const MDagPath &path)
{
	MFnSpotLight light(path);
	MColor color = light.color();
	float intensity = light.intensity();

	MTransformationMatrix xform(path.inclusiveMatrix());
	MVector pos = xform.getTranslation(MSpace::kTransform);
	MFloatVector dir = light.lightDirection();

	// Mirage has no light type carrying a direction/cone-angle concept at
	// all, so a spot's cone falloff cannot be represented - this is the
	// weakest of the emissive-sphere approximations. Offsetting the sphere
	// slightly along the spot's direction at least biases where the light
	// visually originates from, rather than behaving exactly like an
	// omnidirectional point light in the same position; the cone itself is
	// simply not there; geometry outside where the real cone would be will
	// still be lit.
	const float radius = 0.05f;
	Mirage::Vec3 emission = Mirage::Vec3(color.r, color.g, color.b) * intensity * m_intensityScale * SphereAreaCompensation(radius);

	Mirage::Vec3 offsetPos(
		static_cast<float>(pos.x) + dir.x * radius,
		static_cast<float>(pos.y) + dir.y * radius,
		static_cast<float>(pos.z) + dir.z * radius);

	AddEmissiveSphere(offsetPos, radius, emission);
}

void LightTranslator::TranslateDirectionalLight(const MDagPath &path)
{
	MFnDirectionalLight light(path);
	MColor color = light.color();
	float intensity = light.intensity();

	MTransformationMatrix xform(path.inclusiveMatrix());
	MVector pos = xform.getTranslation(MSpace::kTransform);
	MFloatVector dir = light.lightDirection();

	// Mirage has no true infinite/distant light type - approximate with a
	// large emissive sphere placed far away along the light's direction.
	// This is the weakest mapping of the five: "far away" is an arbitrary
	// fixed distance (not derived from the actual scene's scale), and it
	// changes penumbra/shadow softness in ways a real directional light
	// wouldn't. Deliberately not layering further "compensation" math onto
	// this one the way the point/spot cases do - it would suggest a
	// precision this approximation doesn't have. Use the lightIntensityScale
	// render-globals setting to calibrate brightness for a given scene.
	const float distance = 1000.0f;
	const float radius = 200.0f;
	Mirage::Vec3 farPos(
		static_cast<float>(pos.x) - dir.x * distance,
		static_cast<float>(pos.y) - dir.y * distance,
		static_cast<float>(pos.z) - dir.z * distance);

	Mirage::Vec3 emission = Mirage::Vec3(color.r, color.g, color.b) * intensity * m_intensityScale;

	AddEmissiveSphere(farPos, radius, emission);
}

void LightTranslator::TranslateAreaLight(const MDagPath &path)
{
	MFnAreaLight light(path);
	MColor color = light.color();
	float intensity = light.intensity();

	// Maya's area light is a unit (1x1) quad in the light shape's own local
	// space, scaled/rotated/positioned by its transform - transforming the
	// 4 unit-quad corners by the world matrix directly (rather than trying
	// to derive width/height from some dedicated attribute, which doesn't
	// exist) reproduces whatever size/facing the light's own icon has.
	MMatrix worldMatrix = path.inclusiveMatrix();
	MPoint corners[4] = {
		MPoint(-0.5, -0.5, 0.0) * worldMatrix,
		MPoint(0.5, -0.5, 0.0) * worldMatrix,
		MPoint(0.5, 0.5, 0.0) * worldMatrix,
		MPoint(-0.5, 0.5, 0.0) * worldMatrix,
	};

	// Area lights are Mirage's best-supported case - its own light model
	// *is* emissive geometry, so this is a close match rather than an
	// approximation like the other four.
	Mirage::Vec3 emission = Mirage::Vec3(color.r, color.g, color.b) * intensity * m_intensityScale;

	AddEmissiveQuad(corners, emission);
}

void LightTranslator::TranslateAmbientLight(const MDagPath &path)
{
	MFnAmbientLight light(path);
	MColor color = light.color();
	float intensity = light.intensity();

	// A flat, uniform-from-everywhere contribution is a good match for
	// Mirage's non-probe Sky, which is itself just a horizon/zenith
	// gradient - setting both to the same color makes it flat rather than
	// gradiented. If multiple ambient lights exist in the scene, the last
	// one encountered wins (a documented simplification, not a crash).
	Mirage::Vec3 c = Mirage::Vec3(color.r, color.g, color.b) * intensity * m_intensityScale;
	m_scene.sky.horizon = c;
	m_scene.sky.zenith = c;
	m_sawAmbientLight = true;
}

void LightTranslator::FinalizeSky()
{
	if (m_sawAmbientLight)
		return;

	Mirage::Vec3 dim(0.05f, 0.05f, 0.05f);
	m_scene.sky.horizon = dim;
	m_scene.sky.zenith = dim;
}

void LightTranslator::AddEmissiveSphere(const Mirage::Vec3 &position, float radius, const Mirage::Vec3 &emission)
{
	auto material = std::make_unique<Mirage::Material>();
	material->color = Mirage::Vec3(0.0f);
	material->emission = emission;

	Mirage::Primitive primitive;
	primitive.type = Mirage::eSphere;
	primitive.sphere.radius = radius;
	primitive.startTransform.p = position;
	primitive.endTransform = primitive.startTransform;
	primitive.lightSamples = 1;
	primitive.materialIndex = m_scene.AddMaterial(std::move(material));

	m_scene.AddPrimitive(primitive);
}

void LightTranslator::AddEmissiveQuad(const MPoint corners[4], const Mirage::Vec3 &emission)
{
	auto mesh = std::make_unique<Mirage::Mesh>();

	Mirage::Vec3 v0(static_cast<float>(corners[0].x), static_cast<float>(corners[0].y), static_cast<float>(corners[0].z));
	Mirage::Vec3 v1(static_cast<float>(corners[1].x), static_cast<float>(corners[1].y), static_cast<float>(corners[1].z));
	Mirage::Vec3 v2(static_cast<float>(corners[2].x), static_cast<float>(corners[2].y), static_cast<float>(corners[2].z));
	Mirage::Vec3 v3(static_cast<float>(corners[3].x), static_cast<float>(corners[3].y), static_cast<float>(corners[3].z));

	Mirage::Vec3 normal = Mirage::SafeNormalize(Mirage::Cross(v1 - v0, v2 - v0), Mirage::Vec3(0.0f, 1.0f, 0.0f));

	mesh->vertices = {v0, v1, v2, v3};
	mesh->normals = {normal, normal, normal, normal};
	mesh->uvs = {Mirage::Vec2(0.0f, 0.0f), Mirage::Vec2(1.0f, 0.0f), Mirage::Vec2(1.0f, 1.0f), Mirage::Vec2(0.0f, 1.0f)};
	mesh->indices = {0, 1, 2, 0, 2, 3};
	mesh->rebuildBVH();

	auto material = std::make_unique<Mirage::Material>();
	material->color = Mirage::Vec3(0.0f);
	material->emission = emission;

	Mirage::Primitive primitive;
	primitive.type = Mirage::eMesh;
	primitive.mesh = Mirage::GeometryFromMesh(mesh.get());
	primitive.lightSamples = 1;
	primitive.materialIndex = m_scene.AddMaterial(std::move(material));

	m_scene.AddPrimitive(primitive);
	m_scene.AddMesh(std::move(mesh));
}
