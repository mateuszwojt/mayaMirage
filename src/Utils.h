#pragma once

#include <maya/MStatus.h>
#include <maya/MString.h>
#include <maya/MObject.h>
#include <maya/MSelectionList.h>
#include <maya/MMatrix.h>

#include <mirage/math/Mat44.h>

inline MStatus getDependencyNodeByName(const MString &name, MObject &node)
{
	MSelectionList selList;
	selList.add(name);

	if (selList.isEmpty())
		return MS::kFailure;

	return selList.getDependNode(0, node);
}

// Maya's MMatrix (like USD's GfMatrix4d) is row-vector, post-multiply
// (v' = v * M); Mirage::Mat44 is column-vector, pre-multiply (v' = M * v -
// see Mirage::operator*(Mat44, Vec4)). Converting between the two requires
// an explicit transpose, not a straight element-by-element copy - the same
// gotcha hdMirage's camera/mesh transform code already had to account for
// when converting GfMatrix4d to Mat44.
inline Mirage::Mat44 ToMirageMat44(const MMatrix &m)
{
	return Mirage::Mat44(
		static_cast<float>(m(0, 0)), static_cast<float>(m(1, 0)), static_cast<float>(m(2, 0)), static_cast<float>(m(3, 0)),
		static_cast<float>(m(0, 1)), static_cast<float>(m(1, 1)), static_cast<float>(m(2, 1)), static_cast<float>(m(3, 1)),
		static_cast<float>(m(0, 2)), static_cast<float>(m(1, 2)), static_cast<float>(m(2, 2)), static_cast<float>(m(3, 2)),
		static_cast<float>(m(0, 3)), static_cast<float>(m(1, 3)), static_cast<float>(m(2, 3)), static_cast<float>(m(3, 3)));
}
