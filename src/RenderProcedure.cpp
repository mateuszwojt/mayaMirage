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
	m_renderOptions.clamp = FLT_MAX;
	// maxDepth now comes from RenderGlobalsNode::getRenderOptions() (a real,
	// user-editable render-globals attribute) rather than being hardcoded.

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
	m_session->PrepareRenderer(m_renderOptions.type, m_renderOptions.width, m_renderOptions.height, recreated);
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