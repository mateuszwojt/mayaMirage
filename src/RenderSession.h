#pragma once

#include <cstdint>

#include <mirage/core/Scene.h>
#include <mirage/core/Renderer.h>

// Owns the Mirage::Scene and the currently-active Mirage::Renderer backend
// for one Maya render session. Mirage itself has no scene-dirty-tracking or
// backend-switch mechanism of its own - VulkanRenderer uploads scene
// geometry to the GPU exactly once, ever, at construction, and there is no
// API to ask an existing Renderer to switch CPU/GPU backends in place. A
// host has to track "has anything structurally changed" itself and
// recreate the whole Renderer object when it has - the same pattern
// hdMirage (HdMirageRenderer) and the planned mirage-nuke-plugin port
// already use.
class RenderSession
{
public:
	RenderSession() = default;
	~RenderSession();

	RenderSession(const RenderSession &) = delete;
	RenderSession &operator=(const RenderSession &) = delete;

	Mirage::Scene scene;

	// Call for structural changes only: topology/material/instance-count
	// edits, a mesh added/removed, etc. Do NOT call this for camera moves or
	// pure rigid-transform edits on an already-uploaded primitive - those
	// should instead call GetActiveRenderer()->ResetAccumulation(), which is
	// far cheaper (no scene re-upload, just clears progressive accumulation
	// state).
	void MarkSceneStructurallyDirty();
	uint64_t GetSceneVersion() const { return m_sceneVersion; }

	// Returns the active renderer for (requestedBackend, width, height),
	// recreating it first if needed - the scene was marked structurally
	// dirty since the last upload, the requested backend preference changed,
	// or the output dimensions changed. outRecreated is set to true if a
	// recreate happened this call, so the caller knows to reset its own
	// progressive-sample bookkeeping (a freshly-created Renderer has no
	// accumulated samples).
	//
	// If requestedBackend is eGpu but the Vulkan backend is unavailable
	// (device creation failed, or the Slang runtime/shaders couldn't be
	// resolved), this transparently falls back to the CPU backend and warns
	// via MGlobal::displayWarning - it never throws or returns null. Once
	// that fallback happens, repeated calls with the same requestedBackend
	// do NOT keep retrying the GPU path every call (that would defeat the
	// whole point of the recreate-gate) - only a genuine change in what's
	// being *requested*, or a structural/dimension change, triggers another
	// attempt. GetActiveBackend() reports what's actually running, which may
	// differ from requestedBackend precisely because of this fallback.
	Mirage::Renderer *PrepareRenderer(Mirage::RenderType requestedBackend, int width, int height, bool &outRecreated);

	Mirage::Renderer *GetActiveRenderer() const { return m_pRenderer; }
	Mirage::RenderType GetActiveBackend() const { return m_activeBackend; }

private:
	// Constructs the requested backend, falling back to CPU (with a
	// warning) if a requested GPU backend isn't actually available. Updates
	// m_activeBackend to whatever backend was actually constructed.
	Mirage::Renderer *CreateBackend(Mirage::RenderType requested);

	Mirage::Renderer *m_pRenderer = nullptr;

	// What's actually running right now (post-fallback) vs. what was last
	// asked for (pre-fallback) - kept separate so a GPU->CPU fallback
	// doesn't cause every subsequent PrepareRenderer() call to think the
	// backend "changed" (activeBackend would permanently differ from
	// requestedBackend after a fallback) and retry GPU construction forever.
	Mirage::RenderType m_activeBackend = Mirage::eCpu;
	Mirage::RenderType m_lastRequestedBackend = Mirage::eCpu;

	int m_width = 0;
	int m_height = 0;

	uint64_t m_sceneVersion = 0;
	uint64_t m_uploadedSceneVersion = UINT64_MAX; // sentinel: never uploaded, forces first-use creation
};
