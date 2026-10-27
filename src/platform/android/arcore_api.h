#pragma once

#include <arcore_c_api.h>
#include <string>
#include <godot_cpp/variant/quaternion.hpp>
#include <godot_cpp/variant/transform3d.hpp>

#define ARCORE_FUNCTIONS(F) \
	F(ArSession_create) F(ArSession_destroy) F(ArSession_configure) F(ArSession_getConfig) F(ArSession_resume) F(ArSession_pause) \
	F(ArSession_update) F(ArSession_setDisplayGeometry) F(ArFrame_getUpdatedTrackables) \
	F(ArSession_getSupportedCameraConfigsWithFilter) F(ArSession_setCameraConfig) F(ArSession_getCameraConfig) \
	F(ArConfig_create) F(ArConfig_destroy) F(ArConfig_setTextureUpdateMode) F(ArConfig_setUpdateMode) \
	F(ArConfig_setPlaneFindingMode) F(ArConfig_getPlaneFindingMode) F(ArConfig_setLightEstimationMode) F(ArConfig_setFocusMode) \
	F(ArCameraConfigFilter_create) F(ArCameraConfigFilter_destroy) F(ArCameraConfigFilter_setFacingDirection) \
	F(ArCameraConfigFilter_setTargetFps) F(ArCameraConfigFilter_setDepthSensorUsage) F(ArCameraConfigFilter_setStereoCameraUsage) \
	F(ArCameraConfigList_create) F(ArCameraConfigList_destroy) F(ArCameraConfigList_getSize) F(ArCameraConfigList_getItem) \
	F(ArCameraConfig_create) F(ArCameraConfig_destroy) F(ArCameraConfig_getImageDimensions) F(ArCameraConfig_getTextureDimensions) \
	F(ArCameraConfig_getFpsRange) \
	F(ArFrame_create) F(ArFrame_destroy) F(ArFrame_getTimestamp) F(ArFrame_acquireCamera) \
	F(ArFrame_transformCoordinates2d) F(ArFrame_acquireCameraImage) F(ArFrame_getHardwareBuffer) \
	F(ArCamera_release) F(ArCamera_getTrackingState) F(ArCamera_getTrackingFailureReason) \
	F(ArCamera_getDisplayOrientedPose) F(ArCamera_getPose) F(ArCamera_getProjectionMatrix) \
	F(ArCamera_getImageIntrinsics) F(ArCameraIntrinsics_create) F(ArCameraIntrinsics_destroy) \
	F(ArCameraIntrinsics_getFocalLength) F(ArCameraIntrinsics_getPrincipalPoint) F(ArCameraIntrinsics_getImageDimensions) \
	F(ArPose_create) F(ArPose_destroy) F(ArPose_getPoseRaw) \
	F(ArImage_getFormat) F(ArImage_getWidth) F(ArImage_getHeight) F(ArImage_getNumberOfPlanes) \
	F(ArImage_getPlaneData) F(ArImage_getPlanePixelStride) F(ArImage_getPlaneRowStride) F(ArImage_getTimestamp) F(ArImage_release) \
	F(ArTrackableList_create) F(ArTrackableList_destroy) F(ArTrackableList_getSize) F(ArTrackableList_acquireItem) \
	F(ArTrackable_getTrackingState) F(ArTrackable_release) \
	F(ArPlane_acquireSubsumedBy) F(ArPlane_getType) F(ArPlane_getCenterPose) F(ArPlane_getExtentX) F(ArPlane_getExtentZ) \
	F(ArPlane_getPolygonSize) F(ArPlane_getPolygon) \
	F(ArFrame_hitTest) F(ArFrame_hitTestRay) F(ArHitResultList_create) F(ArHitResultList_destroy) \
	F(ArHitResultList_getSize) F(ArHitResultList_getItem) F(ArHitResult_create) F(ArHitResult_destroy) \
	F(ArHitResult_getDistance) F(ArHitResult_getHitPose) F(ArHitResult_acquireTrackable) \
	F(ArTrackable_getType) F(ArPlane_isPoseInPolygon) \
	F(ArRecordingConfig_create) F(ArRecordingConfig_destroy) F(ArRecordingConfig_setMp4DatasetUri) \
	F(ArRecordingConfig_setAutoStopOnPause) F(ArRecordingConfig_addTrack) \
	F(ArTrack_create) F(ArTrack_destroy) F(ArTrack_setId) F(ArTrack_setMetadata) F(ArTrack_setMimeType) \
	F(ArSession_startRecording) F(ArSession_stopRecording) F(ArSession_getRecordingStatus) \
	F(ArSession_setPlaybackDatasetUri) F(ArSession_getPlaybackStatus) F(ArFrame_recordTrackData) \
	F(ArFrame_getUpdatedTrackData) F(ArTrackDataList_create) F(ArTrackDataList_destroy) \
	F(ArTrackDataList_getSize) F(ArTrackDataList_acquireItem) F(ArTrackData_getFrameTimestamp) F(ArTrackData_getData) F(ArTrackData_release) \
	F(ArFrame_acquirePointCloud) F(ArPointCloud_getNumberOfPoints) F(ArPointCloud_getData) \
	F(ArPointCloud_release)

struct ARCoreApi {
#define DECLARE_ARCORE(name) decltype(&::name) name = nullptr;
	ARCORE_FUNCTIONS(DECLARE_ARCORE)
#undef DECLARE_ARCORE
	bool load(std::string &r_error);
};

ARCoreApi &arcore_api();

namespace godot {

inline Transform3D read_arcore_pose(const ARCoreApi &p_api, const ArSession *p_session, const ArPose *p_pose) {
	float raw[7];
	p_api.ArPose_getPoseRaw(p_session, p_pose, raw);
	return Transform3D(Basis(Quaternion(raw[0], raw[1], raw[2], raw[3]).normalized()), Vector3(raw[4], raw[5], raw[6]));
}

}
