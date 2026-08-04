#pragma once

#include <maya/MPxNode.h>

#include <mirage/core/Renderer.h>

#include "../ImageWriter.h"

class RenderGlobalsNode : public MPxNode
{
public:
	static const MString name;
	static const MTypeId id;

	static void *creator();
	static MStatus initialize();

	MStatus compute(const MPlug &plug, MDataBlock &dataBlock) override;

	static void clean();

	static Mirage::Options getRenderOptions();

	// Sane, non-zero fallback used both to seed the node's own attribute
	// defaults (initialize()) and as what getRenderOptions() returns when
	// "defaultMirageRenderGlobals" doesn't exist yet (e.g. a render kicked
	// off before Render Settings' Mirage tab has ever been opened, which is
	// what actually creates the node - see MirageMaya/globals.py's
	// create_render_globals_node()). Mirage::Options has no default member
	// initializers, so returning a bare `Mirage::Options()`/`Options{}` here
	// previously meant maxSamples == 0 - the render loop's
	// `samples < maxSamples` condition is false before it ever starts, so
	// the Render View gets a single all-black frame with no error at all.
	static Mirage::Options DefaultOptions();

	// Global calibration knob for the emissive-primitive light
	// approximations LightTranslator builds for point/spot/directional
	// lights - Mirage has no native photometric unit system for them (see
	// LightTranslator.h), so this is an honest, artist-adjustable scale
	// applied on top of each light's own Maya intensity, not a silently
	// buried magic number.
	static float getLightIntensityScale();

	struct MotionBlurSettings
	{
		bool enabled;
		double shutterOpen;  // frame offset relative to the current frame, e.g. -0.25
		double shutterClose; // frame offset relative to the current frame, e.g. +0.25
	};

	// Rigid-transform motion blur only (see MeshTranslator.h /
	// MayaTransformUtils.h) - Mirage::Mesh has a single vertex buffer with
	// no way to represent deforming/skinned geometry moving between shutter
	// samples, only a whole object's rigid transform moving.
	static MotionBlurSettings getMotionBlurSettings();

	// Which of Mirage's AOVs (depth/normal/primId - all single-valued
	// first-hit snapshots, not progressively accumulated like the beauty
	// image, see mirage/core/Renderer.h) to request. Folded directly into
	// Mirage::Options::aovMask, which is a real field on Options already.
	static uint32_t getAovMask();

	// PNG/JPG/BMP/TGA only - see ImageWriter.h for why. This is
	// deliberately a separate, plugin-owned setting rather than an attempt
	// to interpret Maya's own shared numeric MCommonRenderSettingsData::
	// imageFormat field, which enumerates many formats (EXR, DPX, Cineon,
	// ...) Mirage categorically can't produce.
	static ImageOutputFormat getOutputImageFormat();

	// Global escape hatch forcing MeshTranslator's always-bake fallback
	// path for every instance, even rigid+uniform-scale ones that would
	// otherwise share one object-space Mesh (see MeshTranslator.h) - useful
	// for isolating instancing-related bugs without needing a per-object
	// override mechanism.
	static bool getEnableInstancing();

private:
	static MObject gRenderType;
	static MObject gRenderMode;
	static MObject gFilterType;

	static MObject gExposure;
    static MObject gLimit;
    static MObject gNumSamples;
    static MObject gEnableDOF;
    static MObject gMaxDepth;
    static MObject gLightIntensityScale;
    static MObject gMotionBlurEnabled;
    static MObject gShutterOpen;
    static MObject gShutterClose;
    static MObject gEnableDepthAOV;
    static MObject gEnableNormalAOV;
    static MObject gEnablePrimIdAOV;
    static MObject gOutputImageFormat;
    static MObject gEnableInstancing;

	static MObject gNLMWidth;
    static MObject gNLMFalloff;
};