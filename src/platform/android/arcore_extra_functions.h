#pragma once

#define ARCORE_EXTRA_FUNCTIONS(F) \
	F(ArFrame_hitTest) F(ArFrame_hitTestRay) F(ArHitResultList_create) F(ArHitResultList_destroy) \
	F(ArHitResultList_getSize) F(ArHitResultList_getItem) F(ArHitResult_create) F(ArHitResult_destroy) \
	F(ArHitResult_getDistance) F(ArHitResult_getHitPose) F(ArHitResult_acquireTrackable) F(ArHitResult_acquireNewAnchor) \
	F(ArTrackable_getType) F(ArPlane_isPoseInPolygon) F(ArTrackable_acquireNewAnchor) F(ArSession_acquireNewAnchor) \
	F(ArAnchor_getPose) F(ArAnchor_getTrackingState) F(ArAnchor_detach) F(ArAnchor_release) \
	F(ArRecordingConfig_create) F(ArRecordingConfig_destroy) F(ArRecordingConfig_setMp4DatasetUri) \
	F(ArRecordingConfig_setAutoStopOnPause) F(ArRecordingConfig_addTrack) \
	F(ArTrack_create) F(ArTrack_destroy) F(ArTrack_setId) F(ArTrack_setMetadata) F(ArTrack_setMimeType) \
	F(ArSession_startRecording) F(ArSession_stopRecording) F(ArSession_getRecordingStatus) \
	F(ArSession_setPlaybackDatasetUri) F(ArSession_getPlaybackStatus) F(ArFrame_recordTrackData) \
	F(ArFrame_getUpdatedTrackData) F(ArTrackDataList_create) F(ArTrackDataList_destroy) \
	F(ArTrackDataList_getSize) F(ArTrackDataList_acquireItem) F(ArTrackData_getFrameTimestamp) F(ArTrackData_getData) F(ArTrackData_release) \
	F(ArFrame_acquirePointCloud) F(ArPointCloud_getNumberOfPoints) F(ArPointCloud_getData) \
	F(ArPointCloud_getPointIds) F(ArPointCloud_getTimestamp) F(ArPointCloud_release) F(ArFrame_getUpdatedTrackables)
