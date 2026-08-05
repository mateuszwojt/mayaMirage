#include "InstancerTranslator.h"
#include "MaterialTranslator.h"
#include "MayaTransformUtils.h"

#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include <maya/MFnInstancer.h>
#include <maya/MFnMesh.h>
#include <maya/MItMeshPolygon.h>
#include <maya/MObjectArray.h>
#include <maya/MIntArray.h>
#include <maya/MMatrixArray.h>
#include <maya/MDagPathArray.h>
#include <maya/MPoint.h>
#include <maya/MVector.h>
#include <maya/MGlobal.h>
#include <maya/MFn.h>

#include <mirage/utils/Util.h>

#include "../Utils.h"

namespace
{
	struct CornerKey
	{
		int position;
		int normal;
		int uv;

		bool operator==(const CornerKey &other) const
		{
			return position == other.position && normal == other.normal && uv == other.uv;
		}
	};

	struct CornerKeyHash
	{
		size_t operator()(const CornerKey &k) const
		{
			size_t h = std::hash<int>()(k.position);
			h ^= std::hash<int>()(k.normal) + 0x9e3779b9 + (h << 6) + (h >> 2);
			h ^= std::hash<int>()(k.uv) + 0x9e3779b9 + (h << 6) + (h >> 2);
			return h;
		}
	};

	// Builds one merged, corner-deduplicated Mirage::Mesh from `shapePath`'s
	// geometry, across every shading group it has - unlike MeshTranslator
	// (which splits a multi-shading-group mesh into one submesh per
	// group), PointInstancer's v1 scope has only one materialIndex per
	// instancer, so a per-shading-group split isn't representable here (see
	// InstancerTranslator.h's doc comment). Same corner-dedup construction
	// as MeshTranslator::SubMeshBuilder, just not split by shader.
	std::unique_ptr<Mirage::Mesh> BuildPrototypeMesh(const MDagPath &shapePath)
	{
		MFnMesh mMesh(shapePath);

		const bool meshHasUVs = mMesh.numUVs() > 0;
		MString currentUVSet = mMesh.currentUVSetName();

		auto mesh = std::make_unique<Mirage::Mesh>();
		std::unordered_map<CornerKey, int, CornerKeyHash> cornerToIndex;

		for (MItMeshPolygon polyIt(shapePath); !polyIt.isDone(); polyIt.next())
		{
			const unsigned int vertexCount = polyIt.polygonVertexCount();
			std::vector<int> faceCorners(vertexCount);
			const bool faceHasUVs = meshHasUVs && polyIt.hasUVs(currentUVSet);

			for (unsigned int lv = 0; lv < vertexCount; ++lv)
			{
				const int positionIndex = static_cast<int>(polyIt.vertexIndex(static_cast<int>(lv)));
				const int normalIndex = static_cast<int>(polyIt.normalIndex(static_cast<int>(lv)));
				const MPoint pos = polyIt.point(static_cast<int>(lv), MSpace::kObject);
				MVector normal;
				polyIt.getNormal(lv, normal, MSpace::kObject);

				int uvIndex = -1;
				float u = 0.0f, v = 0.0f;
				if (faceHasUVs)
				{
					polyIt.getUVIndex(static_cast<int>(lv), uvIndex, &currentUVSet);
					float2 uv = {0.0f, 0.0f};
					polyIt.getUV(static_cast<int>(lv), uv, &currentUVSet);
					u = uv[0];
					v = uv[1];
				}

				const CornerKey key{positionIndex, normalIndex, uvIndex};
				auto it = cornerToIndex.find(key);
				int idx;
				if (it != cornerToIndex.end())
				{
					idx = it->second;
				}
				else
				{
					idx = static_cast<int>(mesh->vertices.size());
					mesh->vertices.emplace_back(static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(pos.z));
					mesh->normals.emplace_back(static_cast<float>(normal.x), static_cast<float>(normal.y), static_cast<float>(normal.z));
					mesh->uvs.emplace_back(u, v);
					cornerToIndex.emplace(key, idx);
				}
				faceCorners[lv] = idx;
			}

			for (unsigned int i = 1; i + 1 < vertexCount; ++i)
			{
				mesh->indices.push_back(faceCorners[0]);
				mesh->indices.push_back(faceCorners[i]);
				mesh->indices.push_back(faceCorners[i + 1]);
			}
		}

		if (mesh->indices.empty())
			return nullptr; // degenerate prototype (no faces)

		mesh->ComputeTangents();
		mesh->rebuildBVH();
		return mesh;
	}

	// Resolves `protoPaths[i]`'s prototype entry to an actual mesh shape
	// path, if any - the instancer's input hierarchy commonly references a
	// prototype's transform, not its shape directly (MASH/nParticle both
	// wire it up this way), so a transform needs one more step down to its
	// shape. Returns an invalid MDagPath (isValid() == false) if this
	// prototype has no mesh shape at all (locator, curve, ...).
	MDagPath ResolveMeshShape(const MDagPath &protoPath)
	{
		if (protoPath.node().apiType() == MFn::kMesh)
			return protoPath;

		MDagPath shapePath = protoPath;
		if (shapePath.extendToShapeDirectlyBelow(0) != MS::kSuccess)
			return MDagPath();

		return (shapePath.node().apiType() == MFn::kMesh) ? shapePath : MDagPath();
	}
}

void InstancerTranslator::Translate(const MDagPath &instancerPath, Mirage::Scene &scene, MaterialTranslator &materialTranslator,
									 const MotionBlurSettings &motionBlur)
{
	MFnInstancer instFn(instancerPath);

	MDagPathArray protoPaths;
	MMatrixArray matrices;
	MIntArray pathStartIndices;
	MIntArray pathIndices;
	if (instFn.allInstances(protoPaths, matrices, pathStartIndices, pathIndices) != MS::kSuccess)
		return;

	if (protoPaths.length() == 0 || matrices.length() == 0)
		return; // empty instancer (e.g. no particles simulated yet) - nothing to add

	int meshProtoIndex = -1;
	MDagPath protoShapePath;
	for (unsigned int i = 0; i < protoPaths.length(); ++i)
	{
		MDagPath shapePath = ResolveMeshShape(protoPaths[i]);
		if (shapePath.isValid())
		{
			meshProtoIndex = static_cast<int>(i);
			protoShapePath = shapePath;
			break;
		}
	}

	if (meshProtoIndex < 0)
	{
		MGlobal::displayWarning(MString("Mirage: instancer '") + instancerPath.fullPathName() +
								 "' has no mesh prototype - skipped (only mesh prototypes are translated, not "
								 "locators/curves/etc).");
		return;
	}

	if (protoPaths.length() > 1)
	{
		MGlobal::displayWarning(MString("Mirage: instancer '") + instancerPath.fullPathName() +
								 "' references multiple prototypes - only the first mesh prototype is translated "
								 "(Mirage's PointInstancer supports one shape/material per instancer).");
	}

	std::unique_ptr<Mirage::Mesh> protoMesh = BuildPrototypeMesh(protoShapePath);
	if (!protoMesh)
		return;

	// Material: whichever shading engine is assigned to the prototype
	// shape's first face - PointInstancer has one materialIndex for every
	// instance, so per-face/per-shading-group variation on the prototype
	// isn't representable (same simplification as the multi-prototype one
	// above).
	MFnMesh protoMeshFn(protoShapePath);
	MObjectArray shadingEngines;
	MIntArray faceShaderIndices;
	protoMeshFn.getConnectedShaders(protoShapePath.instanceNumber(), shadingEngines, faceShaderIndices);
	MObject shadingEngine = MObject::kNullObj;
	if (faceShaderIndices.length() > 0)
	{
		const int idx = faceShaderIndices[0];
		if (idx >= 0 && idx < static_cast<int>(shadingEngines.length()))
			shadingEngine = shadingEngines[idx];
	}
	const int materialIndex = materialTranslator.TranslateShadingEngine(shadingEngine);

	const int meshIndex = scene.AddMesh(std::move(protoMesh));

	Mirage::PointInstancer instancer;
	instancer.type = Mirage::eMesh;
	instancer.meshIndex = meshIndex;
	instancer.materialIndex = materialIndex;
	instancer.hydraId = static_cast<int32_t>(std::hash<std::string>{}(instancerPath.fullPathName().asChar()));

	// Only the entries whose pathIndices[i] selects our chosen prototype
	// count as instances of it - allInstances() returns one (matrix,
	// pathIndex) pair per (particle, prototype) combination, not one per
	// particle, when an instancer has more than one prototype in rotation.
	bool warnedNonRigid = false;
	for (unsigned int i = 0; i < pathIndices.length() && i < matrices.length(); ++i)
	{
		if (pathIndices[i] != meshProtoIndex)
			continue;

		const DecomposedTransform decomposed = DecomposeMayaMatrix(matrices[i]);
		if (!decomposed.isRigid && !warnedNonRigid)
		{
			warnedNonRigid = true;
			MGlobal::displayWarning(MString("Mirage: instancer '") + instancerPath.fullPathName() +
									 "' has particles with shear/non-uniform scale, which Mirage::Transform can't "
									 "represent - those instances render with an identity/approximate transform.");
		}
		instancer.instanceStart.push_back(decomposed.xform);
	}

	if (instancer.instanceStart.empty())
		return; // every particle used a different prototype than the one we translated

	if (motionBlur.enabled)
	{
		MDagPathArray endProtoPaths;
		MMatrixArray endMatrices;
		MIntArray endPathStartIndices;
		MIntArray endPathIndices;
		WithTimeOffset(motionBlur.shutterClose, [&]()
		{
			instFn.allInstances(endProtoPaths, endMatrices, endPathStartIndices, endPathIndices);
		});

		std::vector<Mirage::Transform> endTransforms;
		endTransforms.reserve(instancer.instanceStart.size());
		for (unsigned int i = 0; i < endPathIndices.length() && i < endMatrices.length(); ++i)
		{
			if (endPathIndices[i] == meshProtoIndex)
				endTransforms.push_back(DecomposeMayaMatrix(endMatrices[i]).xform);
		}

		// Only apply if the particle count for our prototype matches
		// between shutter samples - a mismatch (particles spawning/dying
		// mid-shutter) is treated as "static" (instanceEnd left empty),
		// same "malformed/mismatched optional data degrades safely"
		// convention PointInstancer.h documents for instanceEnd.
		if (endTransforms.size() == instancer.instanceStart.size())
			instancer.instanceEnd = std::move(endTransforms);
	}

	scene.AddInstancer(instancer);
}
