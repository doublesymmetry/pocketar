#pragma once

#include <arcore_c_api.h>
#include "arcore_extra_functions.h"
#include <string>

#define ARCORE_FUNCTIONS(F) \
	F(ArSession_create) F(ArSession_destroy) F(ArSession_configure) F(ArSession_resume) F(ArSession_pause) \
	F(ArSession_update) F(ArSession_setDisplayGeometry) F(ArSession_getAllTrackables) \
	F(ArSession_getSupportedCameraConfigsWithFilter) F(ArSession_setCameraConfig) F(ArSession_getCameraConfig) \
	F(ArConfig_create) F(ArConfig_destroy) F(ArConfig_setTextureUpdateMode) F(ArConfig_setUpdateMode) \
	F(ArConfig_setPlaneFindingMode) F(ArConfig_setLightEstimationMode) F(ArConfig_setDepthMode) \
	F(ArConfig_setInstantPlacementMode) F(ArConfig_setImageStabilizationMode) F(ArConfig_setFocusMode) \
	F(ArConfig_setCloudAnchorMode) F(ArConfig_setGeospatialMode) F(ArConfig_setSemanticMode) \
	F(ArConfig_setAugmentedFaceMode) F(ArConfig_setStreetscapeGeometryMode) \
	F(ArAugmentedImageDatabase_create) F(ArAugmentedImageDatabase_destroy) F(ArConfig_setAugmentedImageDatabase) \
	F(ArCameraConfigFilter_create) F(ArCameraConfigFilter_destroy) F(ArCameraConfigFilter_setFacingDirection) \
	F(ArCameraConfigFilter_setTargetFps) F(ArCameraConfigFilter_setDepthSensorUsage) F(ArCameraConfigFilter_setStereoCameraUsage) \
	F(ArCameraConfigList_create) F(ArCameraConfigList_destroy) F(ArCameraConfigList_getSize) F(ArCameraConfigList_getItem) \
	F(ArCameraConfig_create) F(ArCameraConfig_destroy) F(ArCameraConfig_getImageDimensions) F(ArCameraConfig_getTextureDimensions) \
	F(ArCameraConfig_getFpsRange) F(ArCameraConfig_getDepthSensorUsage) F(ArCameraConfig_getStereoCameraUsage) F(ArCameraConfig_getCameraId) F(ArString_release) \
	F(ArFrame_create) F(ArFrame_destroy) F(ArFrame_getTimestamp) F(ArFrame_acquireCamera) \
	F(ArFrame_transformCoordinates2d) F(ArFrame_acquireCameraImage) F(ArFrame_getHardwareBuffer) \
	F(ArCamera_release) F(ArCamera_getTrackingState) F(ArCamera_getTrackingFailureReason) \
	F(ArCamera_getDisplayOrientedPose) F(ArCamera_getPose) F(ArCamera_getProjectionMatrix) \
	F(ArCamera_getImageIntrinsics) F(ArCamera_getTextureIntrinsics) F(ArCameraIntrinsics_create) F(ArCameraIntrinsics_destroy) \
	F(ArCameraIntrinsics_getFocalLength) F(ArCameraIntrinsics_getPrincipalPoint) F(ArCameraIntrinsics_getImageDimensions) \
	F(ArPose_create) F(ArPose_destroy) F(ArPose_getPoseRaw) \
	F(ArImage_getFormat) F(ArImage_getWidth) F(ArImage_getHeight) F(ArImage_getNumberOfPlanes) \
	F(ArImage_getPlaneData) F(ArImage_getPlanePixelStride) F(ArImage_getPlaneRowStride) F(ArImage_getTimestamp) F(ArImage_release) \
	F(ArTrackableList_create) F(ArTrackableList_destroy) F(ArTrackableList_getSize) F(ArTrackableList_acquireItem) \
	F(ArTrackable_getTrackingState) F(ArTrackable_release) \
	F(ArPlane_acquireSubsumedBy) F(ArPlane_getType) F(ArPlane_getCenterPose) F(ArPlane_getExtentX) F(ArPlane_getExtentZ) \
	F(ArPlane_getPolygonSize) F(ArPlane_getPolygon) \
	ARCORE_EXTRA_FUNCTIONS(F)

struct ARCoreApi {
#define DECLARE_ARCORE(name) decltype(&::name) name = nullptr;
	ARCORE_FUNCTIONS(DECLARE_ARCORE)
#undef DECLARE_ARCORE
	bool load(std::string &r_error);
};

ARCoreApi &arcore_api();
