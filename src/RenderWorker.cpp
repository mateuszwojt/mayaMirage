#include "RenderWorker.h"
#include "ImageWriter.h"
#include "nodes/RenderGlobals.h"

#include <maya/MEventMessage.h>
#include <maya/MGlobal.h>
#include <maya/MRenderView.h>
#include <maya/MRenderUtil.h>
#include <maya/MCommonRenderSettingsData.h>
#include <maya/MFnRenderLayer.h>
#include <maya/MSelectionList.h>
#include <maya/MDagPath.h>
#include <maya/MFnDagNode.h>
#include <maya/MAnimControl.h>

RenderWorker &RenderWorker::Instance()
{
	static RenderWorker instance;
	return instance;
}

RenderWorker::~RenderWorker()
{
	CancelAndWaitForIdle();
}

void RenderWorker::CancelAndWaitForIdle()
{
	if (!m_running.load())
		return;

	RequestCancel();

	// The idle callback (main-thread only) is what actually joins the
	// worker thread and tears everything down (see OnIdle/FinishRender) -
	// pump it directly rather than trying to join m_thread from here, since
	// this can itself be called from the main thread mid-session (e.g.
	// immediately before starting a new render) where blocking on the
	// worker thread without ever running the idle callback that unblocks it
	// would deadlock.
	while (m_running.load())
	{
		OnIdle();
	}
}

void RenderWorker::StartRender(const Mirage::Camera &camera, const Mirage::Options &options)
{
	if (m_running.load())
	{
		MGlobal::displayWarning("Mirage: a render is already in progress - ignoring new render request.");
		return;
	}

	m_camera = camera;
	m_options = options;
	m_cancelRequested.store(false);
	m_completedSamples.store(0);
	m_lastPushedSamples = -1;

	const size_t pixelCount = static_cast<size_t>(options.width) * static_cast<size_t>(options.height);
	m_workingPixels.assign(pixelCount, Mirage::Color());
	m_workingDepth.assign((options.aovMask & Mirage::kAovDepth) ? pixelCount : 0, Mirage::Color());
	m_workingNormal.assign((options.aovMask & Mirage::kAovNormal) ? pixelCount : 0, Mirage::Color());
	m_workingPrimId.assign((options.aovMask & Mirage::kAovPrimId) ? pixelCount : 0, Mirage::Color());
	{
		std::lock_guard<std::mutex> lock(m_bufferMutex);
		m_latestPixels.assign(pixelCount, Mirage::Color());
		m_latestDepth.assign(m_workingDepth.size(), Mirage::Color());
		m_latestNormal.assign(m_workingNormal.size(), Mirage::Color());
		m_latestPrimId.assign(m_workingPrimId.size(), Mirage::Color());
	}

	m_computation.beginComputation(/*showProgressBar=*/true);
	MRenderView::startRender(options.width, options.height);

	m_running.store(true);
	m_idleCallbackId = MEventMessage::addEventCallback("idle", OnIdleCallback, this);

	m_thread = std::thread(&RenderWorker::ThreadMain, this);
}

void RenderWorker::RequestCancel()
{
	m_cancelRequested.store(true);
}

void RenderWorker::ThreadMain()
{
	Mirage::Renderer *renderer = session.GetActiveRenderer();

	Mirage::AovBuffers aovBuffers;
	aovBuffers.depth = m_workingDepth.empty() ? nullptr : m_workingDepth.data();
	aovBuffers.normal = m_workingNormal.empty() ? nullptr : m_workingNormal.data();
	aovBuffers.primId = m_workingPrimId.empty() ? nullptr : m_workingPrimId.data();
	const bool wantAovs = (aovBuffers.depth || aovBuffers.normal || aovBuffers.primId);

	while (m_completedSamples.load() < m_options.maxSamples && !m_cancelRequested.load())
	{
		renderer->Render(m_camera, m_options, m_workingPixels.data(), wantAovs ? &aovBuffers : nullptr);
		m_completedSamples.fetch_add(1);

		{
			std::lock_guard<std::mutex> lock(m_bufferMutex);
			m_latestPixels = m_workingPixels;
			if (wantAovs)
			{
				m_latestDepth = m_workingDepth;
				m_latestNormal = m_workingNormal;
				m_latestPrimId = m_workingPrimId;
			}
		}
	}
}

void RenderWorker::OnIdleCallback(void *clientData)
{
	static_cast<RenderWorker *>(clientData)->OnIdle();
}

void RenderWorker::OnIdle()
{
	const int samples = m_completedSamples.load();

	if (samples > m_lastPushedSamples)
	{
		std::vector<Mirage::Color> pixelSnapshot, depthSnapshot, normalSnapshot, primIdSnapshot;
		{
			std::lock_guard<std::mutex> lock(m_bufferMutex);
			pixelSnapshot = m_latestPixels;
			depthSnapshot = m_latestDepth;
			normalSnapshot = m_latestNormal;
			primIdSnapshot = m_latestPrimId;
		}
		PushPixelsToRenderView(pixelSnapshot, m_options.width, m_options.height);
		PushAovsToRenderView(0, m_options.width - 1, 0, m_options.height - 1, m_options.aovMask,
							 depthSnapshot, normalSnapshot, primIdSnapshot);
		m_lastPushedSamples = samples;
	}

	// MRenderView has no interrupt-detection API of its own (confirmed
	// against the Maya 2027 devkit: MRenderView.h's full public surface is
	// doesRenderEditorExist/setCurrentCamera/getRenderRegion/startRender/
	// startRegionRender/updatePixels/refresh/endRender/setDrawTileBoundary,
	// nothing interrupt-related) - MComputation is Maya's actual long-
	// running-computation-with-cancel primitive.
	if (m_computation.isInterruptRequested())
	{
		m_cancelRequested.store(true);
	}

	const bool finished = samples >= m_options.maxSamples;
	const bool cancelled = m_cancelRequested.load();

	if (finished || cancelled)
	{
		FinishRender();
	}
}

void RenderWorker::FinishRender()
{
	if (m_thread.joinable())
	{
		m_thread.join();
	}

	// One last resolve, in case the worker completed (or was cancelled)
	// between the progress push above and this check, so the Render View
	// always ends up showing the truly-final buffer, not a slightly stale one.
	{
		std::vector<Mirage::Color> pixelSnapshot, depthSnapshot, normalSnapshot, primIdSnapshot;
		{
			std::lock_guard<std::mutex> lock(m_bufferMutex);
			pixelSnapshot = m_latestPixels;
			depthSnapshot = m_latestDepth;
			normalSnapshot = m_latestNormal;
			primIdSnapshot = m_latestPrimId;
		}
		PushPixelsToRenderView(pixelSnapshot, m_options.width, m_options.height);
		PushAovsToRenderView(0, m_options.width - 1, 0, m_options.height - 1, m_options.aovMask,
							 depthSnapshot, normalSnapshot, primIdSnapshot);
	}

	MRenderView::endRender();
	m_computation.endComputation();

	if (m_idleCallbackId != 0)
	{
		MMessage::removeCallback(m_idleCallbackId);
		m_idleCallbackId = 0;
	}

	m_running.store(false);
}

Mirage::Color RenderWorker::ResolveBackendPixel(const Mirage::Color &raw, bool isGpuBackend)
{
	if (isGpuBackend)
	{
		// VulkanRenderer already resolves its persistent GPU-side
		// accumulation buffer into an already-averaged .x/.y/.z on every
		// Render() call.
		return raw;
	}

	// CPU backend accumulates additively (output[i] += sample, .w holds the
	// running filter weight) - the caller must divide.
	const float weight = (raw.w > 0.0f) ? raw.w : 1.0f;
	return Mirage::Color(raw.x / weight, raw.y / weight, raw.z / weight, 1.0f);
}

void RenderWorker::PushPixelsToRenderView(const std::vector<Mirage::Color> &pixels, int width, int height)
{
	if (pixels.empty())
		return;

	const bool isGpu = (session.GetActiveBackend() == Mirage::eGpu);

	std::vector<RV_PIXEL> outPixels(pixels.size());
	for (size_t i = 0; i < pixels.size(); ++i)
	{
		Mirage::Color resolved = ResolveBackendPixel(pixels[i], isGpu);
		outPixels[i].r = resolved.x * 255.0f;
		outPixels[i].g = resolved.y * 255.0f;
		outPixels[i].b = resolved.z * 255.0f;
		outPixels[i].a = 255.0f;
	}

	MRenderView::updatePixels(0, width - 1, 0, height - 1, outPixels.data());
	MRenderView::refresh(0, width - 1, 0, height - 1);
}

namespace
{
	// AOVs are single-valued first-hit snapshots (not progressively
	// accumulated like the beauty buffer), so they never need the CPU/GPU
	// weight-division ResolveBackendPixel does for the beauty buffer - both
	// backends already write them as plain per-pixel values (see
	// mirage/core/Renderer.h's AovBuffers doc). Depth/primId are
	// single-channel; remapped here into something visually meaningful in
	// an 8-bit RGB channel rather than shown as a raw, mostly-black/white
	// unbounded value.
	void FillNormalAov(const std::vector<Mirage::Color> &normal, std::vector<float> &outChannels)
	{
		outChannels.resize(normal.size() * 3);
		for (size_t i = 0; i < normal.size(); ++i)
		{
			// [-1,1] -> [0,1], the standard normal-visualization convention.
			outChannels[i * 3 + 0] = normal[i].x * 0.5f + 0.5f;
			outChannels[i * 3 + 1] = normal[i].y * 0.5f + 0.5f;
			outChannels[i * 3 + 2] = normal[i].z * 0.5f + 0.5f;
		}
	}

	void FillDepthAov(const std::vector<Mirage::Color> &depth, std::vector<float> &outChannels)
	{
		outChannels.resize(depth.size());
		for (size_t i = 0; i < depth.size(); ++i)
		{
			// Depth is unbounded (miss = some large/inf sentinel) - a cheap
			// reciprocal compression into [0,1] for visualization purposes,
			// not a calibrated/linear depth channel.
			const float d = depth[i].x;
			outChannels[i] = 1.0f / (1.0f + std::max(d, 0.0f));
		}
	}

	void FillPrimIdAov(const std::vector<Mirage::Color> &primId, std::vector<float> &outChannels)
	{
		outChannels.resize(primId.size());
		for (size_t i = 0; i < primId.size(); ++i)
		{
			// primId.x is (float)Primitive::hydraId, -1 on miss (see
			// mirage/core/Renderer.h). A real ID channel for compositing
			// needs float/int precision this 8-bit-per-channel Render View
			// path can't provide anyway - this is a debug visualization
			// (arbitrary-looking but deterministic per-ID grayscale), not a
			// precise ID AOV.
			const int id = static_cast<int>(primId[i].x);
			outChannels[i] = (id < 0) ? 0.0f : static_cast<float>((id * 2654435761u) % 256u) / 255.0f;
		}
	}
}

void RenderWorker::PushAovsToRenderView(unsigned int left, unsigned int right, unsigned int bottom, unsigned int top,
										  uint32_t aovMask, const std::vector<Mirage::Color> &depth,
										  const std::vector<Mirage::Color> &normal, const std::vector<Mirage::Color> &primId)
{
	std::vector<RV_AOV> aovs;
	std::vector<float> depthChannels, normalChannels, primIdChannels;

	if ((aovMask & Mirage::kAovDepth) && !depth.empty())
	{
		FillDepthAov(depth, depthChannels);
		aovs.push_back(RV_AOV{1, MString("depth"), depthChannels.data()});
	}
	if ((aovMask & Mirage::kAovNormal) && !normal.empty())
	{
		FillNormalAov(normal, normalChannels);
		aovs.push_back(RV_AOV{3, MString("normal"), normalChannels.data()});
	}
	if ((aovMask & Mirage::kAovPrimId) && !primId.empty())
	{
		FillPrimIdAov(primId, primIdChannels);
		aovs.push_back(RV_AOV{1, MString("primId"), primIdChannels.data()});
	}

	if (aovs.empty())
		return;

	// Re-push the beauty buffer alongside the AOVs in the same call, since
	// updatePixels()'s AOV parameters are additional arguments to the same
	// call that sends the main image, not a separate call.
	std::vector<Mirage::Color> pixelSnapshot;
	{
		std::lock_guard<std::mutex> lock(m_bufferMutex);
		pixelSnapshot = m_latestPixels;
	}
	if (pixelSnapshot.empty())
		return;

	const bool isGpu = (session.GetActiveBackend() == Mirage::eGpu);
	std::vector<RV_PIXEL> outPixels(pixelSnapshot.size());
	for (size_t i = 0; i < pixelSnapshot.size(); ++i)
	{
		Mirage::Color resolved = ResolveBackendPixel(pixelSnapshot[i], isGpu);
		outPixels[i].r = resolved.x * 255.0f;
		outPixels[i].g = resolved.y * 255.0f;
		outPixels[i].b = resolved.z * 255.0f;
		outPixels[i].a = 255.0f;
	}

	MRenderView::updatePixels(left, right, bottom, top, outPixels.data(), false,
							  static_cast<unsigned int>(aovs.size()), aovs.data());
}

void RenderWorker::RenderBatchSynchronous(const Mirage::Camera &camera, const Mirage::Options &options, const MString &cameraName)
{
	Mirage::Renderer *renderer = session.GetActiveRenderer();

	const size_t pixelCount = static_cast<size_t>(options.width) * static_cast<size_t>(options.height);
	std::vector<Mirage::Color> pixels(pixelCount, Mirage::Color());

	for (int i = 0; i < options.maxSamples; ++i)
	{
		renderer->Render(camera, options, pixels.data());
	}

	const bool isGpu = (session.GetActiveBackend() == Mirage::eGpu);
	for (auto &pixel : pixels)
	{
		pixel = ResolveBackendPixel(pixel, isGpu);
	}

	// Resolve the output path/format/frame padding via Maya's own Common
	// Render Settings, so batch output lands exactly where Maya's Render
	// Settings UI says it will - the same convention every other renderer
	// plugin's batch output follows.
	MCommonRenderSettingsData settings;
	MRenderUtil::getCommonRenderSettings(settings);

	MObject layer = MFnRenderLayer::currentLayer();
	const ImageOutputFormat format = RenderGlobalsNode::getOutputImageFormat();
	const MString fileFormat(ImageWriter::FormatExtension(format));

	MStatus status;
	const double frameNumber = MAnimControl::currentTime().value();
	const MString imagePath = settings.getImageName(
		MCommonRenderSettingsData::kFullPathImage, frameNumber, settings.name, cameraName, fileFormat, layer,
		/*createDirectory=*/true, &status);

	if (status != MS::kSuccess || imagePath.length() == 0)
	{
		MGlobal::displayError("Mirage: failed to resolve batch output image path from Common Render Settings.");
		return;
	}

	ImageWriter::WriteImage(imagePath.asChar(), pixels, options.width, options.height, format);
}
