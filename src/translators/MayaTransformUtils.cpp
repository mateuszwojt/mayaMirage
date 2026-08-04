#include "MayaTransformUtils.h"

#include <cmath>

#include <maya/MTransformationMatrix.h>
#include <maya/MVector.h>
#include <maya/MDGContextGuard.h>
#include <maya/MDGContext.h>
#include <maya/MAnimControl.h>
#include <maya/MTime.h>

#include <mirage/math/Mat33.h>

#include "../Utils.h"

DecomposedTransform DecomposeMayaMatrix(const MMatrix &worldMatrix)
{
	MTransformationMatrix xform(worldMatrix);

	double scale[3];
	xform.getScale(scale, MSpace::kTransform);
	double shear[3];
	xform.getShear(shear, MSpace::kTransform);

	const double kTolerance = 1e-4;
	const bool isRigid =
		std::abs(scale[0] - scale[1]) < kTolerance &&
		std::abs(scale[1] - scale[2]) < kTolerance &&
		std::abs(shear[0]) < kTolerance &&
		std::abs(shear[1]) < kTolerance &&
		std::abs(shear[2]) < kTolerance;

	DecomposedTransform result;
	result.isRigid = isRigid;

	if (!isRigid)
	{
		// result.xform stays default-identity (Transform()'s default ctor) -
		// the caller bakes worldMatrix into the mesh's vertices instead.
		return result;
	}

	MVector translation = xform.getTranslation(MSpace::kTransform);
	result.xform.p = Mirage::Vec3(static_cast<float>(translation.x), static_cast<float>(translation.y), static_cast<float>(translation.z));
	result.xform.s = static_cast<float>(scale[0]);

	// Extract rotation via Mirage's own Mat33->Quat conversion rather than
	// hand-porting Maya's MTransformationMatrix::getRotationQuaternion()
	// components directly - Maya's quaternion is defined against its
	// row-vector matrix convention, Mirage's Quat against its column-vector
	// one, and a naive component-for-component copy across two different
	// conventions is exactly the kind of mistake that produced a real,
	// confirmed 180-degree-rotation bug elsewhere in this codebase family
	// (Quat(1,0,0,0) is not identity - Quat's ctor is (x,y,z,w), and
	// Quat() is the real identity). Routing through ToMirageMat44 (already
	// derived and transpose-correct - see its own comment in Utils.h) and
	// then Mirage::Quat(const Mat33&) sidesteps the whole question: this
	// reuses the one already-verified row/column-vector conversion instead
	// of introducing a second, independently-reasoned-about one.
	Mirage::Mat44 rotMat44 = ToMirageMat44(xform.asRotateMatrix());
	Mirage::Vec4 c0 = rotMat44.GetCol(0);
	Mirage::Vec4 c1 = rotMat44.GetCol(1);
	Mirage::Vec4 c2 = rotMat44.GetCol(2);
	Mirage::Mat33 rot3(Mirage::Vec3(c0.x, c0.y, c0.z), Mirage::Vec3(c1.x, c1.y, c1.z), Mirage::Vec3(c2.x, c2.y, c2.z));
	result.xform.r = Mirage::Quat(rot3);

	return result;
}

MMatrix SampleWorldMatrixAt(const MDagPath &path, double frameOffset)
{
	if (frameOffset == 0.0)
		return path.inclusiveMatrix();

	const MTime current = MAnimControl::currentTime();
	const MTime sampleTime(current.value() + frameOffset, MTime::uiUnit());

	// NOTE: `MDGContextGuard guard(MDGContext(sampleTime));` looks
	// equivalent but is a classic C++ "most vexing parse" - it's parsed as
	// a function declaration (a function named `guard` returning
	// MDGContextGuard, taking an MDGContext parameter), not as constructing
	// a guard object at all, so the context switch below would silently
	// never happen and this function would always just return the current-
	// time matrix regardless of frameOffset. Naming the MDGContext as its
	// own local variable sidesteps the ambiguity entirely.
	const MDGContext context(sampleTime);
	MDGContextGuard guard(context);
	return path.inclusiveMatrix();
}
