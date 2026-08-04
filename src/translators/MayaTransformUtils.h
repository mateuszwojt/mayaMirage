#pragma once

#include <maya/MMatrix.h>
#include <maya/MDagPath.h>

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

// Rigid-transform motion blur settings, in the form the translators need:
// two frame offsets (relative to whatever frame is currently being
// rendered) defining a shutter interval. Not the same thing as
// Mirage::Camera::shutterStart/End, which stay a fixed [0,1] once motion
// blur is enabled - see RenderProcedure::translateCamera(). Deforming/
// skinned-mesh motion blur is out of scope: Mirage::Mesh has a single
// vertex buffer with no way to represent geometry moving between shutter
// samples, only a whole object's rigid transform moving.
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
