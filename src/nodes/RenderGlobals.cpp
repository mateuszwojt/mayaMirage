#include "nodes/RenderGlobals.h"
#include "Utils.h"

#include <cmath>

#include <maya/MDGModifier.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnEnumAttribute.h>
#include <maya/MPlug.h>
#include <maya/MPlugArray.h>
#include <maya/MSelectionList.h>
#include <maya/MGlobal.h>

const MString RenderGlobalsNode::name("MirageRendererGlobalsNode");
const MTypeId RenderGlobalsNode::id(0x3ffff);

MObject RenderGlobalsNode::gRenderType;
MObject RenderGlobalsNode::gRenderMode;
MObject RenderGlobalsNode::gFilterType;

MObject RenderGlobalsNode::gExposure;
MObject RenderGlobalsNode::gLimit;
MObject RenderGlobalsNode::gNumSamples;
MObject RenderGlobalsNode::gEnableDOF;
MObject RenderGlobalsNode::gMaxDepth;
MObject RenderGlobalsNode::gLightIntensityScale;
MObject RenderGlobalsNode::gMotionBlurEnabled;
MObject RenderGlobalsNode::gShutterOpen;
MObject RenderGlobalsNode::gShutterClose;
MObject RenderGlobalsNode::gEnableDepthAOV;
MObject RenderGlobalsNode::gEnableNormalAOV;
MObject RenderGlobalsNode::gEnablePrimIdAOV;
MObject RenderGlobalsNode::gOutputImageFormat;
MObject RenderGlobalsNode::gEnableInstancing;

MObject RenderGlobalsNode::gNLMWidth;
MObject RenderGlobalsNode::gNLMFalloff;

void *RenderGlobalsNode::creator()
{
	return (new RenderGlobalsNode);
}

MStatus RenderGlobalsNode::initialize()
{
	MStatus status;
	MFnNumericAttribute numAttr;
    MFnEnumAttribute eAttr;

	// Mirage::Options has no default member initializers on most of its
	// scalar fields (only Filter's own default-constructed member and
	// aovMask are actually defined by "Options opts;" alone) - the previous
	// version of this function read indeterminate stack memory as the
	// "default" values shown for every one of these attributes on a
	// freshly-created render-globals node. Value-initialize (zeroes
	// everything not explicitly set below) and then assign the same sane
	// defaults used elsewhere in the Mirage tools (mirage/tools/scene_renderer/
	// SceneRenderer.cpp, mirage/tools/kernel_validate/KernelValidate.cpp),
	// rather than relying on undefined behavior for the UI's shown defaults.
	Mirage::Options opts{};
	opts.type = Mirage::RenderType::eCpu;
	opts.mode = Mirage::RenderMode::ePathTrace;
	opts.filter = Mirage::Filter(Mirage::FilterType::eFilterGaussian, 1.0f, 2.0f);
	opts.exposure = 1.0f;
	opts.limit = 1000.0f;
	opts.clamp = 10.0f; // NOTE: the CPU backend renders black if this is left at 0
	opts.nlmWidth = 3.0f;
	opts.nlmFalloff = 1.0f;
	opts.maxDepth = 5;
	opts.maxSamples = 16;
	opts.enableDOF = false;

	gRenderType = eAttr.create("renderType", "renderType", opts.type, &status);
	CHECK_MSTATUS(status);
    eAttr.addField("CPU", Mirage::RenderType::eCpu);
    eAttr.addField("GPU", Mirage::RenderType::eGpu);
	addAttribute(gRenderType);

	gRenderMode = eAttr.create("renderMode", "renderMode", opts.mode, &status);
	CHECK_MSTATUS(status);
    eAttr.addField("Normals", Mirage::RenderMode::eNormals);
    eAttr.addField("Complexity", Mirage::RenderMode::eComplexity);
    eAttr.addField("Path Trace", Mirage::RenderMode::ePathTrace);
	addAttribute(gRenderMode);

	gFilterType = eAttr.create("filterType", "filterType", opts.filter.type, &status);
	CHECK_MSTATUS(status);
    eAttr.addField("Box", Mirage::FilterType::eFilterBox);
    eAttr.addField("Gaussian", Mirage::FilterType::eFilterGaussian);
	addAttribute(gFilterType);

	gExposure = numAttr.create("exposure", "exp", MFnNumericData::kFloat, opts.exposure, &status);
	CHECK_MSTATUS(status);
	numAttr.setMin(0.f);
	numAttr.setMax(INFINITY);
	addAttribute(gExposure);

	gLimit = numAttr.create("limit", "lim", MFnNumericData::kFloat, opts.limit, &status);
	CHECK_MSTATUS(status);
	numAttr.setMin(0.f);
	numAttr.setMax(INFINITY);
	addAttribute(gLimit);

	gNumSamples = numAttr.create("numSamples", "samples", MFnNumericData::kInt, opts.maxSamples, &status);
	CHECK_MSTATUS(status);
	numAttr.setMin(0);
	// Previously capped at 16 - an arbitrary limit that makes no sense for
	// a path tracer, where more samples only costs render time and never
	// hurts correctness. A soft cap still exists to keep the UI slider
	// sane, but 4096 is far more realistic for a noisy scene than 16 ever was.
	numAttr.setMax(4096);
	addAttribute(gNumSamples);

	gEnableDOF = numAttr.create("enableDof", "dof", MFnNumericData::kBoolean, opts.enableDOF, &status);
	CHECK_MSTATUS(status);
	addAttribute(gEnableDOF);

	// Previously hardcoded to a literal 4 in RenderProcedure.cpp, ignoring
	// whatever the render-globals UI showed (there was no UI for it at all).
	gMaxDepth = numAttr.create("maxDepth", "maxDepth", MFnNumericData::kInt, opts.maxDepth, &status);
	CHECK_MSTATUS(status);
	numAttr.setMin(1);
	numAttr.setMax(64);
	addAttribute(gMaxDepth);

	// See LightTranslator.h - Mirage has no native photometric unit system
	// for the point/spot/directional emissive-primitive light
	// approximations, so this is an explicit, artist-adjustable calibration
	// knob rather than a hardcoded fudge factor buried in C++.
	gLightIntensityScale = numAttr.create("lightIntensityScale", "lightIntensityScale", MFnNumericData::kFloat, 1.0f, &status);
	CHECK_MSTATUS(status);
	numAttr.setMin(0.0f);
	addAttribute(gLightIntensityScale);

	gMotionBlurEnabled = numAttr.create("motionBlurEnabled", "motionBlur", MFnNumericData::kBoolean, false, &status);
	CHECK_MSTATUS(status);
	addAttribute(gMotionBlurEnabled);

	// Frame offsets relative to whatever frame is being rendered, defining
	// a shutter roughly centered on it - used only at scene-translation
	// time to pick which two real points in time to sample each object's
	// transform at (see MayaTransformUtils.h's SampleWorldMatrixAt); not the
	// same thing as Mirage::Camera::shutterStart/End, which stay a fixed
	// [0,1] once motion blur is enabled (see RenderProcedure::translateCamera).
	gShutterOpen = numAttr.create("shutterOpen", "shutterOpen", MFnNumericData::kFloat, -0.25f, &status);
	CHECK_MSTATUS(status);
	addAttribute(gShutterOpen);

	gShutterClose = numAttr.create("shutterClose", "shutterClose", MFnNumericData::kFloat, 0.25f, &status);
	CHECK_MSTATUS(status);
	addAttribute(gShutterClose);

	gEnableDepthAOV = numAttr.create("enableDepthAOV", "enableDepthAOV", MFnNumericData::kBoolean, false, &status);
	CHECK_MSTATUS(status);
	addAttribute(gEnableDepthAOV);

	gEnableNormalAOV = numAttr.create("enableNormalAOV", "enableNormalAOV", MFnNumericData::kBoolean, false, &status);
	CHECK_MSTATUS(status);
	addAttribute(gEnableNormalAOV);

	gEnablePrimIdAOV = numAttr.create("enablePrimIdAOV", "enablePrimIdAOV", MFnNumericData::kBoolean, false, &status);
	CHECK_MSTATUS(status);
	addAttribute(gEnablePrimIdAOV);

	// PNG/JPG/BMP/TGA only - matches what ImageWriter can actually produce
	// (the only image writer anywhere in the Mirage codebase family is an
	// 8-bit-only vendored stb_image_write.h), not Maya's full format list.
	gOutputImageFormat = eAttr.create("outputImageFormat", "outputImageFormat", 0, &status);
	CHECK_MSTATUS(status);
	eAttr.addField("PNG", 0);
	eAttr.addField("JPG", 1);
	eAttr.addField("BMP", 2);
	eAttr.addField("TGA", 3);
	addAttribute(gOutputImageFormat);

	gEnableInstancing = numAttr.create("enableInstancing", "enableInstancing", MFnNumericData::kBoolean, true, &status);
	CHECK_MSTATUS(status);
	addAttribute(gEnableInstancing);

	gNLMWidth = numAttr.create("nlmWidth", "nlmWidth", MFnNumericData::kFloat, opts.nlmWidth, &status);
	CHECK_MSTATUS(status);
	numAttr.setMin(0);
	addAttribute(gNLMWidth);

	gNLMFalloff = numAttr.create("nlmFalloff", "nlmFalloff", MFnNumericData::kFloat, opts.nlmFalloff, &status);
	CHECK_MSTATUS(status);
	numAttr.setMin(0);
	addAttribute(gNLMFalloff);

	return (MS::kSuccess);
}

MStatus RenderGlobalsNode::compute(const MPlug &plug, MDataBlock &data)
{
	return (MS::kSuccess);
}

void RenderGlobalsNode::clean()
{
	// MDGModifier::deleteNode() expects a *node* MObject, not an attribute
	// MObject - the previous version of this function called it on each
	// attribute template (gRenderType, gExposure, ...), which is not the
	// node this class actually owns an instance of, so every one of those
	// calls failed (silently, since the returned MStatus was never checked).
	// The real target is the single "defaultMirageRenderGlobals" node
	// instance itself.
	MObject mObj;
	MStatus status = getDependencyNodeByName(RenderGlobalsNode::name, mObj);
	if (status != MS::kSuccess || mObj.isNull())
		return;

	MDGModifier modifier;
	CHECK_MSTATUS(modifier.deleteNode(mObj));
	CHECK_MSTATUS(modifier.doIt());
}

Mirage::Options RenderGlobalsNode::getRenderOptions()
{
	MObject mObj;
	// Value-initialize: width/height/clamp aren't set by this function (the
	// caller overwrites width/height/clamp itself today) and Options has no
	// default member initializers for them - leaving them indeterminate is
	// a latent bug waiting for a future caller that forgets to overwrite one.
	Mirage::Options opts{};

	if (getDependencyNodeByName(RenderGlobalsNode::name, mObj) != MS::kSuccess)
	{
		return Mirage::Options();
	}
	
	int renderType;
	MPlug pRenderType(mObj, gRenderType);
	pRenderType.getValue(renderType);
	opts.type = static_cast<Mirage::RenderType>(renderType);

	int renderMode;
	MPlug pRenderMode(mObj, gRenderMode);
	pRenderMode.getValue(renderMode);
	opts.mode = static_cast<Mirage::RenderMode>(renderMode);

	int filterType;
	MPlug pFilterType(mObj, gFilterType);
	pFilterType.getValue(filterType);
	opts.filter = Mirage::Filter(static_cast<Mirage::FilterType>(filterType), 0.75f, 1.0f);

	MPlug pExposure(mObj, gExposure);
	pExposure.getValue(opts.exposure);

	MPlug pLimit(mObj, gLimit);
	pLimit.getValue(opts.limit);

	MPlug pNumSamples(mObj, gNumSamples);
	pNumSamples.getValue(opts.maxSamples);

	MPlug pEnableDOF(mObj, gEnableDOF);
	pEnableDOF.getValue(opts.enableDOF);

	MPlug pMaxDepth(mObj, gMaxDepth);
	pMaxDepth.getValue(opts.maxDepth);

	MPlug pNLMWidth(mObj, gNLMWidth);
	pNLMWidth.getValue(opts.nlmWidth);

	MPlug pNLMFalloff(mObj, gNLMFalloff);
	pNLMFalloff.getValue(opts.nlmFalloff);

	opts.aovMask = getAovMask();

	return opts;
}

float RenderGlobalsNode::getLightIntensityScale()
{
	MObject mObj;
	if (getDependencyNodeByName(RenderGlobalsNode::name, mObj) != MS::kSuccess)
		return 1.0f;

	float scale = 1.0f;
	MPlug pScale(mObj, gLightIntensityScale);
	pScale.getValue(scale);
	return scale;
}

uint32_t RenderGlobalsNode::getAovMask()
{
	MObject mObj;
	if (getDependencyNodeByName(RenderGlobalsNode::name, mObj) != MS::kSuccess)
		return 0;

	uint32_t mask = 0;

	bool enableDepth = false;
	MPlug pDepth(mObj, gEnableDepthAOV);
	pDepth.getValue(enableDepth);
	if (enableDepth)
		mask |= Mirage::kAovDepth;

	bool enableNormal = false;
	MPlug pNormal(mObj, gEnableNormalAOV);
	pNormal.getValue(enableNormal);
	if (enableNormal)
		mask |= Mirage::kAovNormal;

	bool enablePrimId = false;
	MPlug pPrimId(mObj, gEnablePrimIdAOV);
	pPrimId.getValue(enablePrimId);
	if (enablePrimId)
		mask |= Mirage::kAovPrimId;

	return mask;
}

ImageOutputFormat RenderGlobalsNode::getOutputImageFormat()
{
	MObject mObj;
	if (getDependencyNodeByName(RenderGlobalsNode::name, mObj) != MS::kSuccess)
		return ImageOutputFormat::ePng;

	int format = 0;
	MPlug pFormat(mObj, gOutputImageFormat);
	pFormat.getValue(format);

	switch (format)
	{
	case 1:
		return ImageOutputFormat::eJpg;
	case 2:
		return ImageOutputFormat::eBmp;
	case 3:
		return ImageOutputFormat::eTga;
	default:
		return ImageOutputFormat::ePng;
	}
}

bool RenderGlobalsNode::getEnableInstancing()
{
	MObject mObj;
	if (getDependencyNodeByName(RenderGlobalsNode::name, mObj) != MS::kSuccess)
		return true;

	bool enabled = true;
	MPlug pEnabled(mObj, gEnableInstancing);
	pEnabled.getValue(enabled);
	return enabled;
}

RenderGlobalsNode::MotionBlurSettings RenderGlobalsNode::getMotionBlurSettings()
{
	MotionBlurSettings settings{false, -0.25, 0.25};

	MObject mObj;
	if (getDependencyNodeByName(RenderGlobalsNode::name, mObj) != MS::kSuccess)
		return settings;

	MPlug pEnabled(mObj, gMotionBlurEnabled);
	pEnabled.getValue(settings.enabled);

	float shutterOpen = static_cast<float>(settings.shutterOpen);
	MPlug pShutterOpen(mObj, gShutterOpen);
	pShutterOpen.getValue(shutterOpen);
	settings.shutterOpen = shutterOpen;

	float shutterClose = static_cast<float>(settings.shutterClose);
	MPlug pShutterClose(mObj, gShutterClose);
	pShutterClose.getValue(shutterClose);
	settings.shutterClose = shutterClose;

	return settings;
}
