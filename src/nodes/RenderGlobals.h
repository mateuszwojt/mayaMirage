#pragma once

#include <maya/MPxNode.h>

#include <mirage/core/Renderer.h>
#include <mirage/utils/Util.h>

#include "../ImageWriter.h"

class RenderGlobalsNode : public MPxNode
{
public:
	static const MString name;
	static const MTypeId id;

	// The single render-globals node *instance*'s name, as created by
	// MirageMaya/globals.py's create_render_globals_node() (and referenced by
	// every attrFieldSliderGrp/attrEnumOptionMenuGrp/addControl binding in
	// globals.py and ae_template.py) - NOT the same thing as `name` above,
	// which is the node *type* passed to MFnPlugin::registerNode() and used
	// to createNode() an instance of this class in the first place. There is
	// normally no node in the scene literally named "MirageRendererGlobalsNode".
	// All of the getters below (and clean()) need to look up the instance,
	// not the type, so they must use this, not `name`.
	static const MString kInstanceName;

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

	struct SkySettings
	{
		// false (default): Scene::sky stays the flat horizon/zenith gradient
		// LightTranslator already builds from any MFnAmbientLight (or its
		// dim neutral fallback). true: LightTranslator::FinalizeSky() bakes
		// Mirage v1.2.0's analytic Preetham sky (see mirage/lights/Skylight.h)
		// into an HDR probe instead, taking precedence over any ambient
		// light. The sun direction is not a separate control here - it's
		// derived automatically from the first directional light
		// LightTranslator encounters (falling back to a fixed default angle
		// if the scene has none), so enabling this reuses whatever "sun"
		// the artist already placed rather than requiring a second,
		// redundant direction control.
		bool preetham;

		// Standard Preetham atmospheric turbidity parameter - ~2 is a very
		// clear sky, ~6-10 is hazy/overcast-tending. Only read when
		// `preetham` is true.
		float turbidity;
	};

	static SkySettings getSkySettings();

	// Which of Mirage's AOVs (depth/normal/primId/albedo - all single-valued
	// first-hit snapshots by default, see mirage/core/Renderer.h;
	// DefaultOptions() turns on Options::accumulateAovs so depth/normal/
	// albedo progressively converge instead on the CPU backend, primId
	// excepted) to request. Folded directly into Mirage::Options::aovMask,
	// which is a real field on Options already.
	static uint32_t getAovMask();

	// PNG/JPG/BMP/TGA/EXR - see ImageWriter.h for why the list stops there.
	// This is deliberately a separate, plugin-owned setting rather than an
	// attempt to interpret Maya's own shared numeric
	// MCommonRenderSettingsData::imageFormat field, which enumerates many
	// formats (DPX, Cineon, ...) Mirage still categorically can't produce.
	static ImageOutputFormat getOutputImageFormat();

	// Display transform applied (via Mirage v1.3.0's Mirage::ApplyViewTransform,
	// mirage/utils/Util.h) to LDR batch output and the interactive Render
	// View preview alike - EXR output stays raw/untonemapped regardless (see
	// ImageWriter.h). Not folded into Mirage::Options - like
	// getOutputImageFormat() above, it's an output-facing setting Options
	// itself has no field for. Default is eFilmic, matching this plugin's
	// pre-1.3.0 always-filmic behavior.
	static Mirage::ViewTransform getViewTransform();

	// Global escape hatch forcing MeshTranslator's always-bake fallback
	// path for every instance, even rigid+uniform-scale ones that would
	// otherwise share one object-space Mesh (see MeshTranslator.h) - useful
	// for isolating instancing-related bugs without needing a per-object
	// override mechanism.
	static bool getEnableInstancing();

	// Mirage v1.3.0's post-process Non-Local-Means denoise
	// (mirage/filter/NLM.h), using nlmWidth/nlmFalloff below plus the
	// albedo/normal AOVs as cross-bilateral guide buffers (requested
	// automatically when this is on, even if their own AOV checkboxes are
	// off - see RenderWorker.cpp). false by default: NonLocalMeansFilter is
	// never called.
	static bool getEnableDenoise();

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
    static MObject gEnableAlbedoAOV;
    static MObject gOutputImageFormat;
    static MObject gViewTransform;
    static MObject gEnableInstancing;
    static MObject gSkyType;
    static MObject gSkyTurbidity;

	static MObject gEnableDenoise;
	static MObject gNLMWidth;
    static MObject gNLMFalloff;
};