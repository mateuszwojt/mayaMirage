#include <map>
#include <memory>

#include "RenderProcedure.h"
#include "RenderWorker.h"
#include "nodes/RenderGlobals.h"

#include <maya/MSyntax.h>
#include <maya/MArgDatabase.h>
#include <maya/MItDag.h>
#include <maya/MFnMesh.h>
#include <maya/MGlobal.h>
#include <maya/MIOStream.h>
#include <maya/MPlugArray.h>
#include <maya/MPlug.h>
#include <maya/MSelectionList.h>
#include <maya/MDagPath.h>
#include <maya/MFnCamera.h>
#include <maya/MFnAreaLight.h>
#include <maya/MFnTransform.h>
#include <maya/MRenderView.h>

#include <mirage/utils/Util.h>

const char *RenderProcedure::name = "MirageRenderProcedure";

void *RenderProcedure::creator()
{
	return new RenderProcedure();
}

MSyntax RenderProcedure::createSyntax()
{
	MSyntax syntax;
	syntax.addFlag("-c", "-camera", MSyntax::kString);
	syntax.addFlag("-w", "-width", MSyntax::kLong);
	syntax.addFlag("-h", "-height", MSyntax::kLong);
	return syntax;
}

MStatus RenderProcedure::doIt(const MArgList &args)
{
	int width = 1;
	int height = 1;
	MString cameraName;
	
	MArgDatabase argsData(syntax(), args);

	if (!argsData.isFlagSet("-width") || !argsData.isFlagSet("-height") || !argsData.isFlagSet("-camera"))
	{
		std::cout << "Wrong number of argument passed to render procedure" << std::endl;
		return MS::kFailure;
	}
	argsData.getFlagArgument("-width", 0, width);
	argsData.getFlagArgument("-height", 0, height);
	argsData.getFlagArgument("-camera", 0, cameraName);

	m_renderOptions = RenderGlobalsNode::getRenderOptions();
	m_renderOptions.width = width;
	m_renderOptions.height = height;
	// maxDepth now comes from RenderGlobalsNode::getRenderOptions() (a real,
	// user-editable render-globals attribute) rather than being hardcoded.
	//
	// clamp is deliberately NOT overridden here (this used to hardcode it to
	// FLT_MAX, i.e. no clamping at all) - getRenderOptions() already seeds it
	// from DefaultOptions()'s clamp = 10.0f (there's no user-facing "clamp"
	// attribute on RenderGlobalsNode yet to override it with anyway). The
	// CPU backend accumulates additively across samples and divides by
	// sample count at display time, so an unclamped firefly/near-singular
	// sample that blows up to Inf/NaN at a given pixel poisons that pixel's
	// running average permanently - with FLT_MAX, more samples just meant
	// more chances for some pixel to get hit, so the image visibly degraded
	// (progressively darker/noisier, never recovering) the longer a render
	// ran, instead of cleanly converging like a path tracer should.

	// Cancel any render already in flight and wait for its worker thread /
	// idle callback to fully tear down before touching the shared scene -
	// see RenderWorker's class comment for why the scene has to live in
	// that long-lived singleton rather than on this (short-lived,
	// non-undoable) command instance.
	RenderWorker::Instance().CancelAndWaitForIdle();

	try
	{
		initRender(cameraName);
		preRender();
		render();
		postRender();
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << std::endl;
		return MS::kFailure;
	}

	return MS::kSuccess; // rendering continues asynchronously on RenderWorker's background thread
}

void RenderProcedure::preRender()
{
	std::cout << "Starting preRender() procedure" << std::endl;

	// build BVH - must happen before PrepareRenderer() constructs a backend,
	// since a freshly-constructed VulkanRenderer uploads the scene (incl.
	// its BVH) once, eagerly.
	m_session->scene.Build();

	// create (or reuse) the backend for the currently-requested render type -
	// see RenderSession::PrepareRenderer for the recreate-gating/fallback
	// logic. This is also what fixes the previous hardcoded
	// Mirage::CreateCpuRenderer() call, which silently ignored the
	// renderType (CPU/GPU) setting exposed (but never consulted) on
	// RenderGlobalsNode.
	bool recreated = false;
	Mirage::Renderer *renderer = m_session->PrepareRenderer(m_renderOptions.type, m_renderOptions.width, m_renderOptions.height, recreated);

	// Every render kicked off from here (a fresh "Render" click / batch
	// frame) is meant to be an independent, from-scratch progressive
	// sequence - RenderWorker resets its own CPU-side output buffer to zero
	// at the start of every StartRender() call, so the CPU backend (which
	// accumulates entirely into that caller-owned buffer) already restarts
	// cleanly on its own. The GPU (Vulkan) backend does not: it keeps its
	// own persistent accumulation buffer alive *inside the Renderer object*
	// across calls (see RenderWorker::ResolveBackendPixel's comment), and
	// PrepareRenderer() only recreates that object when something
	// structural actually changed (backend/dimensions/scene version) - a
	// second click with identical settings reuses the same instance and its
	// stale, already-converged accumulation. Without this, each subsequent
	// GPU render blends new samples on top of the *previous* render's
	// result instead of starting over, getting brighter every click until
	// it clips to solid white. Safe to call unconditionally even right
	// after a recreate (a fresh Renderer has nothing to reset).
	renderer->ResetAccumulation();
}

void RenderProcedure::initRender(MString camera)
{
	std::cout << "Starting initRender() procedure" << std::endl;

	m_cameraName = camera;
	m_session = &RenderWorker::Instance().session;
	// Start each render from a clean scene - RenderWorker::Instance().session
	// is a long-lived singleton (see RenderWorker's class comment), so
	// without this, every subsequent render would silently accumulate
	// duplicate geometry/materials on top of every previous render's.
	m_session->scene.Clear();

	buildScene(camera);

	// buildScene() just did a full DAG re-traversal into a freshly-cleared
	// Mirage::Scene - there's no incremental diffing here, every render
	// rebuilds the whole scene from scratch. But RenderSession::PrepareRenderer()
	// only recreates the actual Renderer backend when it sees the session
	// marked structurally dirty (see RenderSession.h) - without this call,
	// nothing ever set that flag, so PrepareRenderer() kept reusing the
	// Renderer instance built from the *first* render's scene forever
	// (VulkanRenderer uploads geometry once, at construction, and the CPU
	// backend is likewise built once against that first scene). Any object
	// added/removed/etc. after the first render would update m_session->scene
	// correctly but never reach the renderer that actually produces pixels.
	m_session->MarkSceneStructurallyDirty();
}

void RenderProcedure::render()
{
	std::cout << "Starting render() procedure" << std::endl;
	std::cout << "Image size: " << m_renderOptions.width << "x" << m_renderOptions.height << ", sampling: " << m_renderOptions.maxSamples << std::endl;
	std::cout << "Rendering mode: " << m_renderOptions.mode << std::endl;

	if (MRenderView::doesRenderEditorExist())
	{
		// Kicks off the actual sampling loop on a background thread and
		// returns immediately - progress is pushed into the Render View
		// from Maya's main-thread idle event as samples complete, and
		// Cancel is handled cooperatively (see RenderWorker).
		RenderWorker::Instance().StartRender(m_Camera, m_renderOptions);
	}
	else
	{
		// Batch/headless (Render/mayabatch, or no Render View for any other
		// reason): render fully synchronously and write straight to disk -
		// see RenderWorker::RenderBatchSynchronous's own comment for why
		// this can't reuse the idle-callback-driven interactive path.
		// Maya's own batch driver owns the frame-range loop (advancing
		// current time and invoking this render procedure once per frame),
		// not this plugin - each call here only needs to render/save the
		// single frame Maya has already set current.
		RenderWorker::Instance().RenderBatchSynchronous(m_Camera, m_renderOptions, m_cameraName);
	}
}

void RenderProcedure::postRender()
{
	std::cout << "Starting postRender() procedure (render dispatched, running asynchronously)" << std::endl;
}