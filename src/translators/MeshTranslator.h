#pragma once

#include <maya/MDagPath.h>

#include <mirage/core/Scene.h>

#include "MayaTransformUtils.h"

class MaterialTranslator;

// Translates one Maya mesh shape (any one of its instance paths - object-
// space geometry is instance-independent) into the scene:
//
//  - One object-space Mirage::Mesh per unique shading engine assigned to
//    the shape's faces (Primitive::materialIndex is one index per whole
//    primitive, not per face, so a multi-shading-group mesh is split into
//    one Mesh/Primitive pair per shading group rather than averaging/
//    picking one material for the whole mesh).
//  - Built corner-by-corner (via MItMeshPolygon), deduplicated by
//    (position, normal, uv) index triple - this both fixes the mesh's
//    normals (previously indexed 1:1 against nothing in particular) and
//    correctly unwelds Maya's native face-varying UVs (a UV can differ per
//    face-corner at a shared vertex, e.g. across a UV seam - Mirage's
//    one-UV-per-vertex-index model can't represent that without unwelding).
//  - One Mirage::Primitive per Maya DAG instance of the shape
//    (MDagPath::getAllPathsTo), sharing the same object-space Mesh when an
//    instance's world matrix decomposes to a rigid transform + uniform
//    scale (see MayaTransformUtils.h) - genuine GPU-memory-shared
//    instancing, since Mirage's intersection code applies
//    Primitive::startTransform per-primitive at render time. Instances
//    whose matrix has shear or non-uniform scale (which
//    Mirage::Transform can't represent) instead get their own private,
//    pre-transformed copy of that submesh's geometry.
//
// Known simplification: per-face shading-group assignment is queried once,
// from whichever instance path is passed in, and applied uniformly to every
// instance of the shape - Maya does technically allow per-instance shading
// overrides on an instanced shape (MFnMesh::getConnectedShaders takes an
// instance number for exactly this reason), but that's a rare enough case
// that supporting it isn't worth the added complexity here; every instance
// is assumed to shade identically.
class MeshTranslator
{
public:
	// `enableInstancing = false` forces every instance down the
	// always-bake-a-private-copy fallback path, even ones whose transform
	// would otherwise decompose cleanly and share one object-space Mesh -
	// a global escape hatch for isolating instancing-related bugs (see
	// RenderGlobalsNode::getEnableInstancing).
	static void Translate(const MDagPath &anyInstancePath, Mirage::Scene &scene, MaterialTranslator &materialTranslator,
						   const MotionBlurSettings &motionBlur, bool enableInstancing);
};
