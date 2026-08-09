#pragma once

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#include <maya/MMessage.h>
#include <maya/MComputation.h>

#include <mirage/core/Renderer.h>
#include <mirage/camera/Camera.h>
#include <mirage/utils/Util.h>

#include "RenderSession.h"

// Drives a progressive, cancellable render into Maya's classic Render View
// on a background thread, so RenderProcedure::doIt() (an MPxCommand) can
// return immediately and leave Maya's UI responsive while sampling
// continues.
//
// This is a singleton with a lifetime independent of any single
// RenderProcedure instance, and that's a hard requirement, not a style
// choice: Maya destroys a non-undoable MPxCommand instance immediately
// after doIt() returns (confirmed in the Maya devkit's own MPxCommand.h:
// "If isUndoable returns false, the command instance is destroyed"), so a
// worker thread / Mirage::Scene owned by RenderProcedure itself would be
// left dangling the moment doIt() returns while a render is still running.
// Mirage::Scene also cannot be worked around by copying it into a
// shorter-lived owner afterward - it holds a std::mutex member, making it
// non-copyable and non-movable - so the scene has to be built directly
// inside this long-lived singleton's RenderSession from the start. A single
// shared instance is also simply correct here: Maya's classic Render View
// is one global window, so only one Mirage render can be meaningfully in
// flight at a time regardless of how many RenderProcedure invocations occur.
class RenderWorker
{
public:
	static RenderWorker &Instance();

	RenderWorker(const RenderWorker &) = delete;
	RenderWorker &operator=(const RenderWorker &) = delete;

	// The scene/renderer for the render currently in flight (or about to be
	// started) - RenderProcedure's scene-translation code populates
	// session.scene directly before calling StartRender().
	RenderSession session;

	bool IsRunning() const { return m_running.load(); }

	// Requests cancellation of the render currently in progress (see
	// RequestCancel()) and blocks until the previous render's worker thread
	// and idle callback have fully torn down, so it's always safe to call
	// this immediately before repopulating `session.scene` for a new render.
	void CancelAndWaitForIdle();

	// Kicks off a progressive render of `camera`/`options` into the Render
	// View using whatever renderer session.PrepareRenderer() last prepared,
	// then returns immediately - the actual sampling loop runs on a
	// background thread, with progress pushed to the Render View from
	// Maya's main-thread idle event. Caller (RenderProcedure) must have
	// already populated session.scene and called session.PrepareRenderer()
	// before calling this. Does nothing but warn if a render is already in
	// progress - see IsRunning().
	void StartRender(const Mirage::Camera &camera, const Mirage::Options &options);

	// Renders `camera`/`options` fully synchronously on the calling thread
	// and writes the result to disk via ImageWriter, resolving the output
	// path/format/frame-padding through Maya's own Common Render Settings
	// (MRenderUtil::getCommonRenderSettings/MCommonRenderSettingsData) so
	// output lands exactly where Maya's Render Settings UI says it will.
	//
	// This is deliberately NOT the same threaded/idle-callback-driven path
	// StartRender() uses: Maya's batch renderer (`Render`/`mayabatch`) has
	// no interactive UI, and idle events are tied to the UI event loop being
	// otherwise idle - in a headless batch process that loop may never pump
	// at all, which would mean an idle-callback-driven render never
	// completes. Blocking here is fine (expected, even) for batch rendering
	// - there's no UI responsiveness to preserve. RenderProcedure calls this
	// instead of StartRender() whenever MRenderView::doesRenderEditorExist()
	// is false.
	void RenderBatchSynchronous(const Mirage::Camera &camera, const Mirage::Options &options,
								 const MString &cameraName);

	// Cooperative cancel: the worker thread only checks this between
	// Render() calls. On GPU, each call is one progressive sample, so cancel
	// latency is bounded by roughly one sample's wall-clock time. On CPU,
	// a single call already performs the entire render (see ThreadMain's
	// comment), so cancel latency there is bounded by one full render
	// instead - there is no per-sample granularity to cancel into mid-call
	// without the Mirage CPU backend itself exposing one.
	void RequestCancel();

private:
	RenderWorker() = default;
	~RenderWorker();

	void ThreadMain();
	void OnIdle();
	static void OnIdleCallback(void *clientData);

	void PushPixelsToRenderView(const std::vector<Mirage::Color> &pixels, int width, int height);

	// Pushes AOV buffers (already backend-resolved) into the Render View's
	// own native AOV channel selector via updatePixels()'s numberOfAOVs/
	// pAOVs parameters, alongside the main beauty updatePixels() call.
	// `aovMask` should be m_requestedAovMask (the user's actual AOV
	// checkboxes), not m_options.aovMask - the latter may have denoise's
	// guide-only albedo/normal bits OR'd in, which must never surface as a
	// phantom Render View AOV channel the user didn't ask for. `beauty` is
	// the same (possibly denoised) pixel snapshot the caller already passed
	// to PushPixelsToRenderView - taken as a parameter rather than this
	// function re-reading m_latestPixels itself, so the AOV-attached
	// updatePixels() call below re-sends the exact same beauty frame
	// instead of silently reverting a just-applied denoise.
	void PushAovsToRenderView(unsigned int left, unsigned int right, unsigned int bottom, unsigned int top,
							   uint32_t aovMask, const std::vector<Mirage::Color> &beauty,
							   const std::vector<Mirage::Color> &depth, const std::vector<Mirage::Color> &normal,
							   const std::vector<Mirage::Color> &primId, const std::vector<Mirage::Color> &albedo);

	void FinishRender();

	std::thread m_thread;
	std::atomic<bool> m_running{false};
	std::atomic<bool> m_cancelRequested{false};
	std::atomic<int> m_completedSamples{0};
	int m_lastPushedSamples = -1;

	std::mutex m_bufferMutex;
	std::vector<Mirage::Color> m_workingPixels; // worker-thread-owned
	std::vector<Mirage::Color> m_latestPixels;  // guarded by m_bufferMutex

	// AOV buffers (depth/normal/primId/albedo) - single-valued first-hit
	// snapshots by default (Mirage v1.3.0's Options::accumulateAovs, on
	// unconditionally per RenderGlobalsNode::DefaultOptions(), instead
	// progressively averages depth/normal/albedo on the CPU backend - see
	// mirage/core/Renderer.h's AovBuffers doc). Empty/unused unless the
	// corresponding Options::aovMask bit is set - which, for albedo/normal,
	// may be true even with their own AOV checkboxes off, when denoise's
	// guide-buffer request set it (see m_requestedAovMask below).
	std::vector<Mirage::Color> m_workingDepth, m_workingNormal, m_workingPrimId, m_workingAlbedo; // worker-thread-owned
	std::vector<Mirage::Color> m_latestDepth, m_latestNormal, m_latestPrimId, m_latestAlbedo;    // guarded by m_bufferMutex

	// The AOV bits the user's render-globals checkboxes actually asked for -
	// kept separate from m_options.aovMask, which StartRender() may OR
	// denoise's guide-only kAovAlbedo/kAovNormal bits into. Used wherever an
	// AOV mask needs to reflect "what should the Render View's channel
	// selector show", not "what should Render() populate".
	uint32_t m_requestedAovMask = 0;

	Mirage::Camera m_camera;
	Mirage::Options m_options{};

	// Display transform + (via m_options.exposure) exposure applied when
	// pushing pixels to the Render View, captured once per render
	// (StartRender) from RenderGlobalsNode::getViewTransform() - keeps the
	// interactive preview visually consistent with batch output, which
	// applies the same Mirage::ApplyViewTransform via ImageWriter.
	Mirage::ViewTransform m_viewTransform = Mirage::ViewTransform::eFilmic;

	MComputation m_computation;
	MCallbackId m_idleCallbackId = 0;
};
