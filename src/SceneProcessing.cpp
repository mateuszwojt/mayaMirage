#include "RenderProcedure.h"
#include "nodes/RenderGlobals.h"
#include "translators/SceneTranslator.h"
#include "translators/MayaTransformUtils.h"

#include <cmath>

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
	// This used to go through MFnCamera::getPortFieldOfView(width, height,
	// ...), which correctly applies Film Fit (Fill/Horizontal/Vertical) for
	// the given render resolution - but *also* unconditionally bakes in the
	// camera's Overscan attribute, with no way to opt out. Overscan is a
	// viewport-only staging aid ("allows us to choreograph action outside of
	// the frustum... without having to resort to a dolly or zoom" - see
	// MFnCamera::FilmFit's own doc comment) - it's not meant to affect the
	// actual rendered frame in any production renderer, but
	// getPortFieldOfView() has no way to exclude it. Confirmed against a
	// real scene with Overscan = 1.3: getPortFieldOfView() returned a
	// vertical FOV of 0.719419 rad, ~28% wider than the correct
	// Film-Fit-only value of 0.563197 rad - exactly the "render wider than
	// the viewport" mismatch reported, since Maya's own gate-masked viewport
	// does not bake overscan into what it shows as the actual output frame
	// either.
	//
	// MFnCamera::getRenderingFrustum(windowAspect, left, right, bottom, top)
	// is the correct function for this: confirmed (by comparing its output
	// against getViewingFrustum() with applyOverscan/applySqueeze/
	// applyPanZoom all explicitly false) that it applies Film Fit the same
	// way getPortFieldOfView() does but deliberately excludes overscan and
	// 2D pan/zoom (both interactive-viewport-only, like overscan) - matching
	// what should actually end up in a rendered frame. Its extents are
	// physical values at the camera's near clip plane, not normalized
	// tan(halfAngle) values, so nearClippingPlane() is needed to recover the
	// angle.
	const double renderAspect = double(m_renderOptions.width) / m_renderOptions.height;
	double left = 0.0, right = 0.0, bottom = 0.0, top = 0.0;
	camera.getRenderingFrustum(renderAspect, left, right, bottom, top);

	const double nearClip = camera.nearClippingPlane();
	const double verticalFOV = (nearClip != 0.0) ? 2.0 * std::atan(top / nearClip) : 0.0;
	m_Camera.fov = static_cast<float>(verticalFOV);
	std::cout << "\tCamera FOV : " << m_Camera.fov << std::endl;

	// set focal point and aperture
	//
	// Mirage v1.2.0 adds real physical camera fields (Camera::focalLength/
	// sensorWidth/sensorHeight/fStop) and EffectiveFov()/
	// EffectiveApertureDiameter() helpers that derive FOV/DOF-aperture from
	// them - but EffectiveFov() only activates when focalLength > 0, and its
	// formula (2*atan(sensorHeight/(2*focalLength))) doesn't know about Film
	// Fit or render aspect ratio the way getRenderingFrustum() above does.
	// Setting m_Camera.focalLength here would silently regress exactly the
	// overscan/Film-Fit bug the FOV computation above was written to fix, so
	// it's deliberately left unset (0 = "unset", per Camera.h's own
	// convention) and m_Camera.fov above remains authoritative.
	//
	// DOF aperture is a separate story: this used to stuff Maya's fStop
	// (an f-number, e.g. 5.6) directly into the legacy `aperture` field,
	// which Mirage documents as a scene-world lens *diameter* - not the
	// same unit at all, just a value that happened to produce some blur.
	// Compute the real physical diameter instead, using the same formula
	// Camera::EffectiveApertureDiameter() applies internally
	// (focalLength_mm / fStop, converted mm -> scene-world meters) but
	// against the legacy `aperture` field directly, so DOF benefits from
	// the physical model without touching `focalLength`/`fStop` and
	// thereby triggering EffectiveFov().
	m_Camera.focalPoint = camera.focusDistance();
	const double focalLengthMm = camera.focalLength();
	const double fStop = camera.fStop();
	m_Camera.aperture = (fStop > 0.0) ? static_cast<float>((focalLengthMm / fStop) / 1000.0) : 0.0f;
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

	const RenderGlobalsNode::SkySettings sky = RenderGlobalsNode::getSkySettings();

	SceneTranslator::Translate(m_session->scene, RenderGlobalsNode::getLightIntensityScale(), translatorMotionBlur,
							   RenderGlobalsNode::getEnableInstancing(), sky.preetham, sky.turbidity);
}
