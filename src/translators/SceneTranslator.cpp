#include "SceneTranslator.h"
#include "MeshTranslator.h"
#include "MaterialTranslator.h"
#include "LightTranslator.h"
#include "InstancerTranslator.h"

#include <unordered_set>

#include <maya/MItDag.h>
#include <maya/MFnDagNode.h>
#include <maya/MObjectHandle.h>
#include <maya/MFn.h>
#include <maya/MDagPath.h>

namespace
{
	struct MObjectHandleHash
	{
		size_t operator()(const MObjectHandle &h) const { return static_cast<size_t>(h.hashCode()); }
	};

	bool IsLightType(MFn::Type type)
	{
		switch (type)
		{
		case MFn::kPointLight:
		case MFn::kSpotLight:
		case MFn::kDirectionalLight:
		case MFn::kAreaLight:
		case MFn::kAmbientLight:
			return true;
		default:
			return false;
		}
	}
}

void SceneTranslator::Translate(Mirage::Scene &scene, float lightIntensityScale, const MotionBlurSettings &motionBlur,
								 bool enableInstancing)
{
	MaterialTranslator materialTranslator(scene);
	LightTranslator lightTranslator(scene, lightIntensityScale);

	// MItDag visits one DAG path per parent transform chain, so an
	// instanced shape's underlying MObject is encountered once per instance
	// - dedupe by MObject identity (not by path) so MeshTranslator::Translate
	// runs exactly once per unique shape and handles every instance path
	// internally via MDagPath::getAllPathsTo.
	std::unordered_set<MObjectHandle, MObjectHandleHash> processedShapes;

	for (MItDag it(MItDag::kDepthFirst); !it.isDone(); it.next())
	{
		MObject obj = it.currentItem();
		const MFn::Type type = obj.apiType();

		if (type == MFn::kMesh)
		{
			MFnDagNode dagNode(obj);
			if (dagNode.isIntermediateObject())
				continue;

			MObjectHandle handle(obj);
			if (processedShapes.count(handle) != 0)
				continue;
			processedShapes.insert(handle);

			MDagPath path;
			it.getPath(path);
			MeshTranslator::Translate(path, scene, materialTranslator, motionBlur, enableInstancing);
		}
		else if (IsLightType(type))
		{
			MDagPath path;
			it.getPath(path);
			lightTranslator.Translate(path);
		}
		else if (type == MFn::kInstancer)
		{
			// MASH's Instancer node and nParticle instancing both create a
			// plain "instancer" DAG node - translated via Mirage v1.1.0's
			// procedural point-instancer (Mirage::PointInstancer) rather
			// than MeshTranslator's per-DAG-instance baking, since a
			// particle instancer's "instances" aren't DAG instances at all
			// (see InstancerTranslator.h). Its prototype geometry is
			// translated directly by InstancerTranslator, not by the
			// kMesh branch above, so no dedup-by-shape bookkeeping is
			// needed here the way it is for ordinary meshes.
			MDagPath path;
			it.getPath(path);
			InstancerTranslator::Translate(path, scene, materialTranslator, motionBlur);
		}
	}

	lightTranslator.FinalizeSky();
}
