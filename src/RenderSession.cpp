#include "RenderSession.h"

#include <mirage/core/VulkanRenderer.h>

#include <maya/MGlobal.h>

RenderSession::~RenderSession()
{
	delete m_pRenderer;
}

void RenderSession::MarkSceneStructurallyDirty()
{
	++m_sceneVersion;
}

Mirage::Renderer *RenderSession::CreateBackend(Mirage::RenderType requested)
{
	if (requested == Mirage::eGpu)
	{
		Mirage::Renderer *vk = Mirage::CreateVulkanRenderer(&scene);
		if (static_cast<Mirage::VulkanRenderer *>(vk)->IsAvailable())
		{
			m_activeBackend = Mirage::eGpu;
			return vk;
		}

		MGlobal::displayWarning(
			"Mirage: Vulkan backend unavailable (device creation failed, or the Slang "
			"runtime/shaders could not be resolved) - falling back to the CPU backend.");
		delete vk;
	}

	m_activeBackend = Mirage::eCpu;
	return Mirage::CreateCpuRenderer(&scene);
}

Mirage::Renderer *RenderSession::PrepareRenderer(Mirage::RenderType requestedBackend, int width, int height, bool &outRecreated)
{
	const bool needsRecreate =
		m_pRenderer == nullptr ||
		m_sceneVersion != m_uploadedSceneVersion ||
		requestedBackend != m_lastRequestedBackend ||
		width != m_width ||
		height != m_height;

	outRecreated = needsRecreate;

	if (needsRecreate)
	{
		delete m_pRenderer;
		m_pRenderer = CreateBackend(requestedBackend); // also updates m_activeBackend
		m_pRenderer->Init(width, height);

		m_lastRequestedBackend = requestedBackend;
		m_uploadedSceneVersion = m_sceneVersion;
		m_width = width;
		m_height = height;
	}

	return m_pRenderer;
}
