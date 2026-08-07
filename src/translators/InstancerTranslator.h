#pragma once

#include <maya/MDagPath.h>

#include <mirage/core/Scene.h>

#include "MayaTransformUtils.h"

class MaterialTranslator;

// Translates a Maya "instancer" node (MFnInstancer - the DAG node underlying
// both MASH's Instancer node and nParticle instancing) into a Mirage
// v1.1.0 Mirage::PointInstancer (see mirage/prims/PointInstancer.h),
// registered via Scene::AddInstancer rather than baking one
// Mirage::Primitive per particle by hand - the whole point of using
// Mirage's own procedural instancer instead of re-deriving that expansion
// here (Scene::Build()/ExpandInstancers() does it once, scene-side).
//
// v1 scope, matching Mirage::PointInstancer's own v1 scope (one template
// shape, one material - see PointInstancer.h's doc comment): only the
// first mesh prototype referenced by the instancer's input hierarchy is
// translated, and every particle shares that one prototype's material,
// regardless of how many distinct prototypes/shading groups the instancer
// actually has (warned once, not per-particle - same spirit as
// MeshTranslator.h's per-instance-shading-override "Known simplification").
// Non-mesh prototypes (locators, curves, ...) and particles with a
// shear/non-uniform-scale transform (which Mirage::Transform can't
// represent, same limitation ordinary DAG instancing already has) are
// skipped/approximated with a once-per-instancer warning, not a crash.
class InstancerTranslator
{
public:
	static void Translate(const MDagPath &instancerPath, Mirage::Scene &scene, MaterialTranslator &materialTranslator,
						   const MotionBlurSettings &motionBlur);
};
