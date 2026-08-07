#include "MeshTranslator.h"
#include "MaterialTranslator.h"
#include "MayaTransformUtils.h"

#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include <maya/MFnMesh.h>
#include <maya/MItMeshPolygon.h>
#include <maya/MObjectArray.h>
#include <maya/MIntArray.h>
#include <maya/MDagPathArray.h>
#include <maya/MPoint.h>
#include <maya/MVector.h>
#include <maya/MFloatVector.h>

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

	// Accumulates one shading-group's worth of corner-deduplicated,
	// object-space geometry before it becomes a real Mirage::Mesh.
	struct SubMeshBuilder
	{
		std::unique_ptr<Mirage::Mesh> mesh = std::make_unique<Mirage::Mesh>();
		std::unordered_map<CornerKey, int, CornerKeyHash> cornerToIndex;

		int GetOrAddCorner(const CornerKey &key, const MPoint &pos, const MVector &nrm, float u, float v)
		{
			auto it = cornerToIndex.find(key);
			if (it != cornerToIndex.end())
				return it->second;

			const int idx = static_cast<int>(mesh->vertices.size());
			mesh->vertices.emplace_back(static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(pos.z));
			mesh->normals.emplace_back(static_cast<float>(nrm.x), static_cast<float>(nrm.y), static_cast<float>(nrm.z));
			mesh->uvs.emplace_back(u, v);
			cornerToIndex.emplace(key, idx);
			return idx;
		}
	};
}

void MeshTranslator::Translate(const MDagPath &anyInstancePath, Mirage::Scene &scene, MaterialTranslator &materialTranslator,
								const MotionBlurSettings &motionBlur, bool enableInstancing)
{
	MFnMesh mMesh(anyInstancePath);
	MObject shapeObj = anyInstancePath.node();

	// Per-face shading-group assignment - queried once, from this instance,
	// and applied uniformly to every instance of this shape (see
	// MeshTranslator.h's "Known simplification" note).
	MObjectArray shadingEngines;
	MIntArray faceShaderIndices;
	mMesh.getConnectedShaders(anyInstancePath.instanceNumber(), shadingEngines, faceShaderIndices);

	const bool meshHasUVs = mMesh.numUVs() > 0;
	MString currentUVSet = mMesh.currentUVSetName();

	std::unordered_map<int, SubMeshBuilder> subMeshes; // keyed by shading-engine index into shadingEngines, or -1 for unassigned

	for (MItMeshPolygon polyIt(anyInstancePath); !polyIt.isDone(); polyIt.next())
	{
		const int faceIndex = static_cast<int>(polyIt.index());
		const int shaderIdx = (faceIndex < static_cast<int>(faceShaderIndices.length())) ? faceShaderIndices[faceIndex] : -1;

		const unsigned int vertexCount = polyIt.polygonVertexCount();
		std::vector<int> faceCorners(vertexCount);

		SubMeshBuilder &builder = subMeshes[shaderIdx]; // default-constructs on first use for this shading engine

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

			CornerKey key{positionIndex, normalIndex, uvIndex};
			faceCorners[lv] = builder.GetOrAddCorner(key, pos, normal, u, v);
		}

		// Fan-triangulate this face within its shading-group submesh.
		for (unsigned int i = 1; i + 1 < vertexCount; ++i)
		{
			builder.mesh->indices.push_back(faceCorners[0]);
			builder.mesh->indices.push_back(faceCorners[i]);
			builder.mesh->indices.push_back(faceCorners[i + 1]);
		}
	}

	if (subMeshes.empty())
		return; // degenerate mesh (no faces) - nothing to add

	// Deforming (2-keyframe) vertex motion blur (Mirage v1.1.0's
	// Mesh::verticesEnd): only worth the extra full re-sample of point
	// positions at shutter-close for meshes that can actually move between
	// shutter samples independent of their transform (skinned/blend-shaped
	// geometry) - see HasUpstreamDeformer. Static/rigidly-transformed
	// meshes keep verticesEnd empty (HasMotion() == false), exactly as
	// before this feature, and get only the existing per-instance
	// transform blur below.
	if (motionBlur.enabled && HasUpstreamDeformer(anyInstancePath))
	{
		for (auto &entry : subMeshes)
			// Fallback for any corner not revisited below (shouldn't happen -
			// see comment in the WithTimeOffset block) - "no motion" for that
			// corner rather than a garbage/zero position.
			entry.second.mesh->verticesEnd = entry.second.mesh->vertices;

		WithTimeOffset(motionBlur.shutterClose, [&]()
		{
			for (MItMeshPolygon polyIt(anyInstancePath); !polyIt.isDone(); polyIt.next())
			{
				const int faceIndex = static_cast<int>(polyIt.index());
				const int shaderIdx = (faceIndex < static_cast<int>(faceShaderIndices.length())) ? faceShaderIndices[faceIndex] : -1;

				auto found = subMeshes.find(shaderIdx);
				if (found == subMeshes.end())
					continue;
				SubMeshBuilder &builder = found->second;

				const unsigned int vertexCount = polyIt.polygonVertexCount();
				const bool faceHasUVs = meshHasUVs && polyIt.hasUVs(currentUVSet);

				for (unsigned int lv = 0; lv < vertexCount; ++lv)
				{
					// Topology (which position/normal/uv index a given
					// face-corner maps to) is time-invariant under
					// deformation - only the values those indices point at
					// change - so the exact same CornerKey construction as
					// the shutter-open pass above reliably resolves to the
					// same corner's index via cornerToIndex, without needing
					// to also re-derive normals/UVs (only the end-of-shutter
					// position is needed here).
					const int positionIndex = static_cast<int>(polyIt.vertexIndex(static_cast<int>(lv)));
					const int normalIndex = static_cast<int>(polyIt.normalIndex(static_cast<int>(lv)));
					const MPoint pos = polyIt.point(static_cast<int>(lv), MSpace::kObject);

					int uvIndex = -1;
					if (faceHasUVs)
						polyIt.getUVIndex(static_cast<int>(lv), uvIndex, &currentUVSet);

					const CornerKey key{positionIndex, normalIndex, uvIndex};
					auto cornerIt = builder.cornerToIndex.find(key);
					if (cornerIt == builder.cornerToIndex.end())
						continue; // shouldn't happen given time-invariant topology

					builder.mesh->verticesEnd[cornerIt->second] = Mirage::Vec3(
						static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(pos.z));
				}
			}
		});
	}

	MDagPathArray allPaths;
	MDagPath::getAllPathsTo(shapeObj, allPaths);

	const int32_t stableId = static_cast<int32_t>(std::hash<std::string>{}(anyInstancePath.fullPathName().asChar()));

	for (auto &entry : subMeshes)
	{
		const int shaderIdx = entry.first;
		SubMeshBuilder &builder = entry.second;

		if (builder.mesh->indices.empty())
			continue; // shouldn't happen, but don't register an empty mesh if it does

		int materialIndex;
		if (shaderIdx >= 0 && shaderIdx < static_cast<int>(shadingEngines.length()))
			materialIndex = materialTranslator.TranslateShadingEngine(shadingEngines[shaderIdx]);
		else
			materialIndex = materialTranslator.TranslateShadingEngine(MObject::kNullObj);

		// Tangent-space normal mapping (Mirage v1.1.0): ComputeTangents()
		// only requires normals.size() == vertices.size(), which the
		// corner-dedup builder above already guarantees - deliberately NOT
		// preceded by a CalculateNormals() call, which would discard the
		// authored/per-corner normals just built (hard edges, custom vertex
		// normals) in favor of pure face-normal averaging. A no-UV mesh
		// leaves tangents empty (HasTangents() == false) - safe no-op, same
		// as every other new-in-v1.1.0 field.
		builder.mesh->ComputeTangents();

		builder.mesh->rebuildBVH();

		bool sharedMeshUsed = false;

		for (unsigned int i = 0; i < allPaths.length(); ++i)
		{
			// Without motion blur, both samples are the same instant (the
			// current frame) and startTransform == endTransform - no blur.
			// With it, two real time-samples are taken via
			// SampleWorldMatrixAt (an MDGContextGuard-based, current-time-
			// preserving evaluation), and the renderer's own per-ray time
			// sample interpolates between the two resulting transforms
			// (see mirage/core/Intersection.h's InterpolateTransform) - no
			// renderer-side change needed, just correct population of the
			// two snapshots.
			const MMatrix worldMatrixStart = motionBlur.enabled
				? SampleWorldMatrixAt(allPaths[i], motionBlur.shutterOpen)
				: allPaths[i].inclusiveMatrix();
			const MMatrix worldMatrixEnd = motionBlur.enabled
				? SampleWorldMatrixAt(allPaths[i], motionBlur.shutterClose)
				: worldMatrixStart;

			const DecomposedTransform decomposedStart = DecomposeMayaMatrix(worldMatrixStart);
			const DecomposedTransform decomposedEnd = DecomposeMayaMatrix(worldMatrixEnd);

			Mirage::Primitive primitive;
			primitive.type = Mirage::eMesh;
			primitive.materialIndex = materialIndex;
			primitive.hydraId = stableId;

			if (enableInstancing && decomposedStart.isRigid && decomposedEnd.isRigid)
			{
				// Shared object-space mesh: Mirage's intersection code
				// applies this primitive's own transform at render time
				// (see mirage/core/Intersection.h), so every rigid instance
				// can point at the exact same Mesh/BVH - real,
				// GPU-memory-shared instancing, not a baked-per-instance copy.
				primitive.startTransform = decomposedStart.xform;
				primitive.endTransform = decomposedEnd.xform;
				primitive.mesh = Mirage::GeometryFromMesh(builder.mesh.get());
				sharedMeshUsed = true;
				scene.AddPrimitive(primitive);
			}
			else
			{
				// Shear/non-uniform scale (at either shutter sample):
				// Mirage::Transform can't represent it, so bake this
				// instance's own private, pre-transformed copy of the
				// submesh instead, at the shutter-open sample only - no
				// motion blur for this instance (a mesh has one vertex
				// buffer; there's no way to also encode "and it deformed
				// like this by shutter-close" once baked). Deliberately
				// copies only the vector fields (not the whole Mesh object)
				// - Mesh owns a raw-pointer BVH with no user-defined copy
				// constructor (mirage/prims/Mesh.h deletes bvh.nodes in
				// ~Mesh()), so a naive `Mesh copy = *builder.mesh;` after
				// rebuildBVH() above would shallow-copy that pointer and
				// double-free it.
				// Deforming vertex motion blur (verticesEnd) is deliberately
				// NOT carried into the baked copy, for the same single-
				// vertex-buffer reason startTransform/endTransform blur
				// isn't: this instance already only gets one static, pre-
				// transformed snapshot (at worldMatrixStart) - there's no
				// second buffer to also bake worldMatrixEnd/deformed
				// positions into.
				auto bakedMesh = std::make_unique<Mirage::Mesh>();
				bakedMesh->vertices = builder.mesh->vertices;
				bakedMesh->normals = builder.mesh->normals;
				bakedMesh->uvs = builder.mesh->uvs;
				bakedMesh->indices = builder.mesh->indices;
				bakedMesh->tangents = builder.mesh->tangents;
				bakedMesh->Transform(ToMirageMat44(worldMatrixStart));
				bakedMesh->rebuildBVH();

				primitive.mesh = Mirage::GeometryFromMesh(bakedMesh.get());
				scene.AddPrimitive(primitive);
				scene.AddMesh(std::move(bakedMesh));
			}
		}

		if (sharedMeshUsed)
		{
			scene.AddMesh(std::move(builder.mesh));
		}
		// else: builder.mesh was only ever a geometry template for baked
		// per-instance copies above and is destroyed here, unused.
	}
}
