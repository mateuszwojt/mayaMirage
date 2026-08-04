#pragma once

#include <maya/MPxCommand.h>

#include <mirage/core/Scene.h>
#include <mirage/core/Renderer.h>

#include "RenderSession.h"

// Note on lifetime: this MPxCommand instance is destroyed by Maya
// immediately after doIt() returns (isUndoable() is false - see
// MPxCommand.h). Rendering itself happens asynchronously on RenderWorker's
// singleton-owned background thread, which outlives doIt() by design (that's
// the whole point of a non-blocking progressive Render View) - so nothing
// that needs to survive past doIt() returning may live on this class. Scene
// translation below writes directly into RenderWorker::Instance().session
// (via m_session, a non-owning pointer to it) rather than a scene owned
// here; m_renderOptions/m_Camera are plain value types copied into the
// worker by StartRender() before doIt() returns.
class RenderProcedure : public MPxCommand
{
public:
	static const char *name;
	static void *creator();
	static MSyntax createSyntax();

	~RenderProcedure() override = default;

	MStatus doIt(const MArgList &arg) override;

	bool isUndoable() const override { return false; }
	bool hasSyntax() const override { return true; }

	void buildScene(MString cameraName);
	void translateCamera(MString cameraName);
	// Mesh/light translation is handled by SceneTranslator (see
	// translators/SceneTranslator.h) - it replaces what used to be
	// per-RenderProcedure translateLights()/translateGeo() methods.

	void preRender();
	void initRender(MString camera);
	void render();
	void postRender();

private:
	RenderSession          *m_session = nullptr; // non-owning; points at RenderWorker::Instance().session
    Mirage::Options         m_renderOptions;
    Mirage::Camera          m_Camera;
    MString                 m_cameraName; // needed by RenderWorker::RenderBatchSynchronous's getImageName() call
};