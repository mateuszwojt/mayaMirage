#pragma once

#include <mirage/core/Scene.h>

#include "MayaTransformUtils.h"

// Orchestrates a full-DAG-walk scene translation pass: every mesh (via
// MeshTranslator/MaterialTranslator) and every light (via LightTranslator)
// currently in the scene gets translated into `scene` exactly once.
class SceneTranslator
{
public:
	// `lightIntensityScale` is the RenderGlobalsNode-exposed calibration
	// knob (see LightTranslator.h) - Mirage has no native photometric unit
	// system for the point/spot/directional light approximations, so this
	// is an honest, artist-adjustable global scale rather than a buried
	// magic number. `motionBlur` feeds MeshTranslator's rigid-transform
	// motion blur (see MayaTransformUtils.h). `enableInstancing = false`
	// forces MeshTranslator's always-bake fallback for every instance (see
	// MeshTranslator.h).
	static void Translate(Mirage::Scene &scene, float lightIntensityScale, const MotionBlurSettings &motionBlur,
						   bool enableInstancing);
};
