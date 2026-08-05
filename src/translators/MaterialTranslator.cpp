#include "MaterialTranslator.h"

#include <algorithm>
#include <cmath>

#include <maya/MFnDependencyNode.h>
#include <maya/MFnStandardSurfaceShader.h>
#include <maya/MFnLambertShader.h>
#include <maya/MFnPhongShader.h>
#include <maya/MFnBlinnShader.h>
#include <maya/MColor.h>
#include <maya/MPlugArray.h>
#include <maya/MRenderUtil.h>
#include <maya/MGlobal.h>
#include <maya/MFn.h>

namespace
{
	// A shadingEngine node's actual shader is reached via its
	// "surfaceShader" input plug's incoming connection - shadingEngine
	// nodes themselves carry no shading parameters of their own.
	MObject GetSurfaceShaderFromShadingEngine(const MObject &shadingEngine)
	{
		MFnDependencyNode sgFn(shadingEngine);
		MPlug surfaceShaderPlug = sgFn.findPlug("surfaceShader", false);
		if (surfaceShaderPlug.isNull() || !surfaceShaderPlug.isConnected())
			return MObject::kNullObj;

		MPlugArray connections;
		surfaceShaderPlug.connectedTo(connections, true, false);
		if (connections.length() == 0)
			return MObject::kNullObj;

		return connections[0].node();
	}

	// standardSurface has no exact "specularTint"/"sheenTint" analog -
	// Maya's specularColor/sheenColor are full RGB tint colors, while
	// Mirage's specularTint/sheenTint are a single 0..1 "how saturated is
	// the tint" scalar layered on top of the base color. Approximate via
	// the color's saturation (0 = achromatic = Material's own default of
	// no tint, up to 1 for a fully-saturated color) - documented as an
	// approximation, not an exact reproduction.
	float ApproximateTintFromColor(const MColor &c)
	{
		float maxC = std::max({c.r, c.g, c.b});
		float minC = std::min({c.r, c.g, c.b});
		return (maxC > 1e-4f) ? (maxC - minC) / maxC : 0.0f;
	}
}

int MaterialTranslator::TranslateShadingEngine(const MObject &shadingEngine)
{
	std::string key;
	if (shadingEngine.isNull())
	{
		key = "__mirage_placeholder_material__";
	}
	else
	{
		MFnDependencyNode sgFn(shadingEngine);
		key = sgFn.name().asChar();
	}

	auto cached = m_cache.find(key);
	if (cached != m_cache.end())
		return cached->second;

	std::unique_ptr<Mirage::Material> material;
	if (shadingEngine.isNull())
	{
		// No shading engine assigned at all (getConnectedShaders() reported
		// -1 for this face) - fall back to a flat placeholder rather than
		// crashing or leaving materialIndex unassigned.
		material = std::make_unique<Mirage::Material>();
		material->color = Mirage::Vec3(0.4f, 0.6f, 0.2f);
	}
	else
	{
		MObject shaderNode = GetSurfaceShaderFromShadingEngine(shadingEngine);
		material = shaderNode.isNull() ? std::make_unique<Mirage::Material>() : TranslateSurfaceShader(shaderNode);
	}

	int index = m_scene.FindOrAddMaterial(key, std::move(material));
	m_cache.emplace(key, index);
	return index;
}

std::unique_ptr<Mirage::Material> MaterialTranslator::TranslateSurfaceShader(const MObject &shaderNode)
{
	MFnDependencyNode shaderFn(shaderNode);
	MString typeName = shaderFn.typeName();

	if (typeName == "standardSurface")
		return TranslateStandardSurface(shaderNode);

	if (typeName == "lambert" || typeName == "blinn" || typeName == "phong" || typeName == "phongE")
		return TranslateLegacyShader(shaderNode);

	MGlobal::displayWarning(MString("Mirage: shader type '") + typeName +
							 "' is not supported for translation - using a default material for '" +
							 shaderFn.name() + "'.");
	return std::make_unique<Mirage::Material>();
}

std::unique_ptr<Mirage::Material> MaterialTranslator::TranslateStandardSurface(const MObject &shaderNode)
{
	MFnDependencyNode shaderFn(shaderNode);
	MFnStandardSurfaceShader surf(shaderNode);

	auto material = std::make_unique<Mirage::Material>();

	const float base = surf.base();
	const MColor baseColor = surf.baseColor();
	material->color = Mirage::Vec3(baseColor.r * base, baseColor.g * base, baseColor.b * base);

	material->metallic = surf.metalness();
	material->roughness = surf.specularRoughness();
	material->specular = surf.specular();
	material->specularTint = ApproximateTintFromColor(surf.specularColor());

	material->subsurface = surf.subsurface();
	material->sheen = surf.sheen();
	material->sheenTint = ApproximateTintFromColor(surf.sheenColor());

	material->clearcoat = surf.coat();
	// Mirage's clearcoatGloss is a gloss (higher = glossier); standardSurface's
	// coatRoughness is a roughness (higher = rougher) - invert.
	material->clearcoatGloss = 1.0f - surf.coatRoughness();

	material->anisotropic = surf.specularAnisotropy();
	material->transmission = surf.transmission();

	const float emissionStrength = surf.emission();
	const MColor emissionColor = surf.emissionColor();
	material->emission = Mirage::Vec3(emissionColor.r, emissionColor.g, emissionColor.b) * emissionStrength;

	const float ior = surf.specularIOR();
	material->eta = (ior > 0.0f) ? ior : 0.0f; // 0 => Material::GetIndexOfRefraction() infers from `specular`

	// Opacity: Mirage's opacity is a single cutout scalar (1.0 = fully
	// opaque, see Material.h); standardSurface's `opacity` attribute is an
	// RGB tint, so use its luminance as the scalar default. A connected
	// `file` texture (its alpha channel, sampled per-hit) overrides the
	// scalar, same priority convention as the albedo/roughness/metallic
	// slots below.
	const MColor opacityColor = surf.opacity();
	material->opacity = (opacityColor.r + opacityColor.g + opacityColor.b) / 3.0f;

	// Textures: only the four channels Mirage::Material has a texture slot
	// for at all (albedo/roughness/metallic/opacity), plus normalCamera's
	// own bump2d-mediated network below. baseColor/specularColor
	// connections are color data (sRGB-decoded); roughness/metalness/opacity
	// connections are non-color data (must stay linear) - matches
	// TextureColorSpace's documented UsdPreviewSurface-style convention.
	int albedoTex = ResolveFileTexture(shaderFn.findPlug("baseColor", false), Mirage::TextureColorSpace::eSRGB);
	if (albedoTex >= 0)
		material->albedoTextureIndex = albedoTex;

	int roughnessTex = ResolveFileTexture(shaderFn.findPlug("specularRoughness", false), Mirage::TextureColorSpace::eLinear);
	if (roughnessTex >= 0)
		material->roughnessTextureIndex = roughnessTex;

	int metallicTex = ResolveFileTexture(shaderFn.findPlug("metalness", false), Mirage::TextureColorSpace::eLinear);
	if (metallicTex >= 0)
		material->metallicTextureIndex = metallicTex;

	int opacityTex = ResolveFileTexture(shaderFn.findPlug("opacity", false), Mirage::TextureColorSpace::eLinear);
	if (opacityTex >= 0)
		material->opacityTextureIndex = opacityTex;

	int normalTex = ResolveNormalMapTexture(shaderFn.findPlug("normalCamera", false));
	if (normalTex >= 0)
		material->normalTextureIndex = normalTex;

	return material;
}

std::unique_ptr<Mirage::Material> MaterialTranslator::TranslateLegacyShader(const MObject &shaderNode)
{
	MFnDependencyNode shaderFn(shaderNode);
	// Blinn/Phong/PhongE all derive from Lambert, so its color/transparency
	// accessors work uniformly across all of them.
	MFnLambertShader lambert(shaderNode);

	auto material = std::make_unique<Mirage::Material>();

	const MColor color = lambert.color();
	material->color = Mirage::Vec3(color.r, color.g, color.b);

	// Same luminance-of-tint-color approach as TranslateStandardSurface's
	// opacity handling above, just starting from `transparency` (1 =
	// fully transparent) rather than an `opacity` attribute directly.
	const MColor transparency = lambert.transparency();
	const float transparencyLuminance = (transparency.r + transparency.g + transparency.b) / 3.0f;
	material->opacity = std::max(0.0f, 1.0f - transparencyLuminance);

	const MString typeName = shaderFn.typeName();
	if (typeName == "phong" || typeName == "phongE")
	{
		MFnPhongShader phong(shaderNode);
		const float cosPower = phong.cosPower();
		// Approximate inverse mapping from a Phong specular exponent to a
		// GGX-style roughness - a documented heuristic, not a physically
		// exact conversion (higher exponent = tighter highlight = lower
		// roughness).
		material->roughness = std::max(0.02f, std::min(1.0f, 1.0f / std::sqrt(std::max(cosPower, 1.0f))));
		material->specular = 0.5f;
	}
	else if (typeName == "blinn")
	{
		MFnBlinnShader blinn(shaderNode);
		const float eccentricity = blinn.eccentricity(); // already roughly a 0..1 roughness-like knob
		material->roughness = std::max(0.02f, std::min(1.0f, eccentricity));
		material->specular = 1.0f - blinn.specularRollOff();
	}
	else
	{
		// Plain lambert: purely diffuse, no specular lobe to approximate.
		material->roughness = 1.0f;
		material->specular = 0.0f;
	}

	int albedoTex = ResolveFileTexture(shaderFn.findPlug("color", false), Mirage::TextureColorSpace::eSRGB);
	if (albedoTex >= 0)
		material->albedoTextureIndex = albedoTex;

	return material;
}

int MaterialTranslator::ResolveFileTexture(const MPlug &plug, Mirage::TextureColorSpace colorSpace)
{
	if (plug.isNull() || !plug.isConnected())
		return -1;

	MPlugArray connections;
	plug.connectedTo(connections, true, false);

	MObject fileNode = MObject::kNullObj;
	for (unsigned int i = 0; i < connections.length(); ++i)
	{
		if (connections[i].node().hasFn(MFn::kFileTexture))
		{
			fileNode = connections[i].node();
			break;
		}
	}

	if (fileNode.isNull())
		return -1; // connected to something other than a plain file texture (e.g. a procedural/layered network) - not supported

	MStatus status;
	MString resolvedPath = MRenderUtil::exactFileTextureName(fileNode, &status);
	if (status != MS::kSuccess || resolvedPath.length() == 0)
		return -1;

	const std::string path = resolvedPath.asChar();

	std::unique_ptr<Mirage::Texture> texture = Mirage::LoadTextureFromFile(path, colorSpace);
	if (!texture)
	{
		MGlobal::displayWarning(MString("Mirage: failed to load texture '") + resolvedPath + "'.");
		return -1;
	}

	return m_scene.FindOrAddTexture(path, std::move(texture));
}

int MaterialTranslator::ResolveNormalMapTexture(const MPlug &plug)
{
	if (plug.isNull() || !plug.isConnected())
		return -1;

	MPlugArray connections;
	plug.connectedTo(connections, true, false);
	if (connections.length() == 0)
		return -1;

	const MObject bumpNode = connections[0].node();
	if (!bumpNode.hasFn(MFn::kBump))
		return -1; // only the standard file->bump2d->shader network is supported

	MFnDependencyNode bumpFn(bumpNode);

	// bump2d's `bumpInterp` attribute: 0 = "Bump" (height-field, no
	// Mirage::Material field to map onto), 1 = "Tangent Space Normals"
	// (exactly what Material::normalTextureIndex expects), 2 = "Object
	// Space Normals" (not tangent-space, also not representable). Only
	// mode 1 is translated.
	MPlug interpPlug = bumpFn.findPlug("bumpInterp", false);
	int interp = 0;
	if (!interpPlug.isNull())
		interpPlug.getValue(interp);
	if (interp != 1)
		return -1;

	return ResolveFileTexture(bumpFn.findPlug("bumpValue", false), Mirage::TextureColorSpace::eLinear);
}
