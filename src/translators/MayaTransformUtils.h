#pragma once

#include <maya/MMatrix.h>
#include <maya/MDagPath.h>
#include <maya/MDGContextGuard.h>
#include <maya/MDGContext.h>
#include <maya/MAnimControl.h>
#include <maya/MTime.h>

#include <mirage/math/Transform.h>

// Result of decomposing a Maya world matrix for use as a Mirage
// Primitive::startTransform/endTransform. Mirage::Transform only supports
// translation + rotation + a single UNIFORM scale factor (see
// mirage/math/Transform.h) - it cannot represent shear or non-uniform
// scale.
struct DecomposedTransform
{
	Mirage::Transform xform;

	// True: `xform` faithfully represents `worldMatrix` (rigid transform,
	// uniform scale) - safe to use directly as a Primitive's transform
	// while sharing one object-space Mesh across every instance using it
	// (Mirage's intersection code applies this transform per-primitive at
	// render time - see mirage/core/Intersection.h - so this is genuine,
	// GPU-memory-shared instancing, not baked-per-instance geometry).
	//
	// False: `worldMatrix` has shear and/or non-uniform scale that
	// Mirage::Transform cannot represent - `xform` is left at identity, and
	// the caller must instead bake `worldMatrix` directly into a private,
	// per-instance copy of the mesh's object-space vertices/normals (see
	// Mirage::Mesh::Transform) and leave that primitive's transform at
	// identity.
	bool isRigid;
};

// Maya's MMatrix (like USD's GfMatrix4d) is row-vector, post-multiply
// (v' = v * M); Mirage's Transform/Mat44 types are column-vector,
// pre-multiply (v' = M * v). DecomposeMayaMatrix accounts for this - the
// returned Quat/translation are correct to feed directly into a
// Mirage::Transform, no further conversion needed.
DecomposedTransform DecomposeMayaMatrix(const MMatrix &worldMatrix);

// Motion blur settings, in the form the translators need: two frame offsets
// (relative to whatever frame is currently being rendered) defining a
// shutter interval. Not the same thing as Mirage::Camera::shutterStart/End,
// which stay a fixed [0,1] once motion blur is enabled - see
// RenderProcedure::translateCamera(). Every DAG instance gets rigid-
// transform blur (see DecomposeMayaMatrix/SampleWorldMatrixAt below); meshes
// with deformer history additionally get per-vertex deforming blur (Mirage
// v1.1.0's Mesh::verticesEnd - see HasUpstreamDeformer and MeshTranslator.cpp)
// - the two are independent and both apply when both are present.
struct MotionBlurSettings
{
	bool enabled = false;
	double shutterOpen = 0.0;
	double shutterClose = 0.0;
};

// Evaluates `path`'s world matrix at (whatever frame Maya currently has
// set) + frameOffset, without disturbing Maya's actual current-time state -
// via MDGContextGuard, a scope object built for exactly this "sample at
// another time, then restore" use case. Passing frameOffset == 0.0 just
// returns path.inclusiveMatrix() directly (no context-switching overhead
// for the common non-motion-blurred case).
MMatrix SampleWorldMatrixAt(const MDagPath &path, double frameOffset);

// Runs `fn` with Maya's current time temporarily shifted by `frameOffset`
// (frame units), restoring it afterwards - the same MDGContextGuard-based
// "sample at another time" mechanism SampleWorldMatrixAt uses above,
// factored out as a template so callers that need more than just a world
// matrix out of the sampled time (e.g. MeshTranslator's deforming-vertex
// position sampling) can reuse the exact same, already-verified context-
// switching idiom instead of re-deriving it (see SampleWorldMatrixAt's own
// comment about the MDGContextGuard-construction most-vexing-parse pitfall).
template <typename Fn>
void WithTimeOffset(double frameOffset, Fn &&fn)
{
	if (frameOffset == 0.0)
	{
		fn();
		return;
	}

	const MTime current = MAnimControl::currentTime();
	const MTime sampleTime(current.value() + frameOffset, MTime::uiUnit());
	const MDGContext context(sampleTime);
	MDGContextGuard guard(context);
	fn();
}

// True if `shapePath`'s construction history has any deformer (skinCluster,
// blendShape, cluster, wire, lattice, ... - anything deriving from Maya's
// geometryFilter base type) upstream of it. Used to gate the extra cost of
// deforming (2-keyframe) vertex motion blur (Mirage v1.1.0's
// Mesh::verticesEnd) to meshes that can actually deform between shutter
// samples - a rigid/static mesh re-sampled at shutter-close would just
// waste time producing verticesEnd identical to vertices.
bool HasUpstreamDeformer(const MDagPath &shapePath);
