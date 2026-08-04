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
	// aspect ratio - the same convention as gluPerspective's fovy).
	//
	// MFnCamera::verticalFieldOfView() is computed purely from the camera
	// node's own intrinsic properties (focal length, film aperture, lens
	// squeeze ratio, camera scale) - it has no width/height parameters, so
	// it cannot know (and does not account for) the aspect ratio of the
	// actual render/port we're rendering into. Maya's Film Fit mode (Fill/
	// Horizontal/Vertical/Overscan) is specifically about reconciling the
	// camera's own film-back aspect ratio against a differently-aspected
	// render resolution, and doing that correctly requires knowing that
	// resolution - which is exactly what getPortFieldOfView(width, height,
	// ...) takes. Using verticalFieldOfView() here meant the render FOV was
	// only ever correct when the render resolution happened to match the
	// camera's film-back aspect ratio; any mismatch (e.g. rendering at
	// 960x540 with a camera whose film back doesn't natively work out to a
	// 16:9 aspect) made the render systematically wider/narrower than what
	// the Maya viewport (which does apply film fit against its own panel
	// size) actually shows for the same camera.
	double horizontalFOV = 0.0, verticalFOV = 0.0;
	camera.getPortFieldOfView(m_renderOptions.width, m_renderOptions.height, horizontalFOV, verticalFOV);
	m_Camera.fov = static_cast<float>(verticalFOV);
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
