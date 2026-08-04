#include "RenderProcedure.h"
#include "nodes/RenderGlobals.h"
#include "translators/SceneTranslator.h"
#include "translators/MayaTransformUtils.h"

#include <mirage/utils/Util.h>

#include <maya/MDagPath.h>
#include <maya/MQuaternion.h>
#include <maya/MSelectionList.h>
#include <maya/MFnCamera.h>
#include <maya/MFnTransform.h>

void RenderProcedure::translateCamera(MString cameraName)
{
	std::cout << "Starting translateCamera() procedure" << std::endl;
	MDagPath mDag;
	MSelectionList mList;
	mList.add(cameraName);
	mList.getDagPath(0, mDag);
	MFnCamera camera(mDag);
	MPoint mOrigin = camera.eyePoint(MSpace::kWorld);
	MPoint mInterestPoint = camera.centerOfInterestPoint(MSpace::kWorld);

	// set camera position and rotation
	m_Camera.position = Mirage::Vec3(mOrigin.x, mOrigin.y, mOrigin.z);
	std::cout << "\tCamera position : " << m_Camera.position.x << ", " << m_Camera.position.y << ", " << m_Camera.position.z << std::endl;

	// Do NOT reuse `mDag` (whatever cameraName happened to resolve to) here -
	// MFnCamera is forgiving about that (it auto-descends from a transform's
	// path to find its camera shape below, so `camera` above is correctly
	// attached either way), but MFnTransform has no equivalent auto-ascend
	// from a shape to its parent transform. If cameraName resolves to the
	// camera *shape* (e.g. Maya's renderer -renderProcedure mechanism has
	// been observed passing the shape name, not the transform), mXform below
	// would silently attach to nothing usable and getRotation() would fail
	// without altering `rotation` from its default-constructed identity -
	// meaning the render camera's orientation would never track Maya's
	// actual camera rotation at all (only its position, via eyePoint() above,
	// which MFnCamera resolves correctly regardless). Deriving the transform
	// path from camera's own confirmed shape path instead is correct no
	// matter which form cameraName came in as.
	MDagPath transformDag = camera.dagPath();
	transformDag.pop();

	MFnTransform mXform(transformDag);
	MQuaternion rotation;
	mXform.getRotation(rotation, MSpace::kWorld);
	m_Camera.rotation = Mirage::Quat(rotation.x, rotation.y, rotation.z, rotation.w);
	std::cout << "\tCamera rotation : " << m_Camera.rotation.x << ", " << m_Camera.rotation.y << ", " << m_Camera.rotation.z << ", " << m_Camera.rotation.w << std::endl;

	// set camera FOV
	// Mirage::Camera::fov is the *vertical* full-angle FOV (see
	// CameraSampler in mirage/utils/Util.h, which uses it unmodified for the
	// vertical screen scale and derives the horizontal scale from it via the
	// aspect ratio - the same convention as gluPerspective's fovy). Use
	// MFnCamera::verticalFieldOfView() directly rather than deriving it by
	// hand from horizontal film aperture, since that also correctly accounts
	// for film fit, overscan, lens squeeze ratio and camera scale.
	MStatus status;
	float fov = camera.verticalFieldOfView(&status);
	m_Camera.fov = fov;
	std::cout << "\tCamera FOV : " << m_Camera.fov << std::endl;

	// set focal point and aperture
	m_Camera.focalPoint = camera.focusDistance();
	m_Camera.aperture = camera.fStop();
	std::cout << "\tCamera focal point : " << m_Camera.focalPoint << std::endl;
	std::cout << "\tCamera aperture : " << m_Camera.aperture << std::endl;

	// Camera-transform motion blur (the camera itself moving) is not
	// implemented - only rigid-object motion blur (see MeshTranslator.h).
	// shutterStart/End just need to span [0,1] for the renderer's own
	// per-ray time sample (Lerp(shutterStart, shutterEnd, u)) to actually
	// interpolate between each primitive's startTransform/endTransform at
	// all - when motion blur is off, leaving them equal means every ray's
	// interpolated time collapses back to the same instant, i.e. no blur,
	// regardless of what any primitive's start/endTransform happen to be.
	const RenderGlobalsNode::MotionBlurSettings motionBlur = RenderGlobalsNode::getMotionBlurSettings();
	m_Camera.shutterStart = 0.0f;
	m_Camera.shutterEnd = motionBlur.enabled ? 1.0f : 0.0f;
}

void RenderProcedure::buildScene(MString cameraName)
{
	std::cout << "Starting buildScene() procedure" << std::endl;
	translateCamera(cameraName);

	// Mesh (materials, instancing, face-varying UVs, per-shading-group
	// splitting) and light translation both live in SceneTranslator/
	// MeshTranslator/MaterialTranslator/LightTranslator now - see
	// translators/SceneTranslator.h.
	const RenderGlobalsNode::MotionBlurSettings mb = RenderGlobalsNode::getMotionBlurSettings();
	MotionBlurSettings translatorMotionBlur{mb.enabled, mb.shutterOpen, mb.shutterClose};

	SceneTranslator::Translate(m_session->scene, RenderGlobalsNode::getLightIntensityScale(), translatorMotionBlur,
							   RenderGlobalsNode::getEnableInstancing());
}
