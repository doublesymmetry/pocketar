#include "arcore_bridge.h"
#include "arcore_interface.h"
#include <mutex>

static std::mutex bridge_mutex;
static ARCoreBridgeState bridge;
static godot::ARCoreInterface *owner = nullptr;

ARCoreBridgeState arcore_bridge_snapshot() {
	std::lock_guard<std::mutex> lock(bridge_mutex);
	return bridge;
}

void arcore_bridge_set_owner(godot::ARCoreInterface *p_owner) {
	owner = p_owner;
}

ARCoreJNIEnv::ARCoreJNIEnv(JavaVM *p_vm) : vm(p_vm) {
	if (vm && vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) == JNI_EDETACHED) {
		attached = vm->AttachCurrentThread(&env, nullptr) == JNI_OK;
	}
}
ARCoreJNIEnv::~ARCoreJNIEnv() {
	if (attached) vm->DetachCurrentThread();
}

static std::string java_string(JNIEnv *p_env, jstring p_value) {
	if (!p_value) return {};
	const char *text = p_env->GetStringUTFChars(p_value, nullptr);
	std::string value = text ? text : "";
	if (text) p_env->ReleaseStringUTFChars(p_value, text);
	return value;
}

static void notify_owner() {
	if (owner) owner->bridge_changed();
}

extern "C" {
JNIEXPORT void JNICALL Java_org_doublesymmetry_pocketar_PocketARPlugin_nativeInitialize(JNIEnv *p_env, jobject, jobject p_context) {
	std::lock_guard<std::mutex> lock(bridge_mutex);
	p_env->GetJavaVM(&bridge.vm);
	if (bridge.context) p_env->DeleteGlobalRef(bridge.context);
	bridge.context = p_env->NewGlobalRef(p_context);
}
JNIEXPORT void JNICALL Java_org_doublesymmetry_pocketar_PocketARPlugin_nativeStatus(JNIEnv *p_env, jobject, jint p_state, jstring p_version) {
	{
		std::lock_guard<std::mutex> lock(bridge_mutex);
		bridge.state = p_state;
		bridge.apk_version = java_string(p_env, p_version);
		bridge.state_history.push_back(p_state);
	}
	notify_owner();
}
JNIEXPORT void JNICALL Java_org_doublesymmetry_pocketar_PocketARPlugin_nativeResume(JNIEnv *, jobject) {
	{ std::lock_guard<std::mutex> lock(bridge_mutex); bridge.resumed = true; }
	notify_owner();
}
JNIEXPORT void JNICALL Java_org_doublesymmetry_pocketar_PocketARPlugin_nativePause(JNIEnv *, jobject) {
	{ std::lock_guard<std::mutex> lock(bridge_mutex); bridge.resumed = false; }
	notify_owner();
}
JNIEXPORT void JNICALL Java_org_doublesymmetry_pocketar_PocketARPlugin_nativeDisplayGeometry(JNIEnv *, jobject, jint p_rotation, jint p_width, jint p_height) {
	std::lock_guard<std::mutex> lock(bridge_mutex);
	bridge.rotation = p_rotation;
	bridge.width = p_width;
	bridge.height = p_height;
	++bridge.geometry_revision;
}
JNIEXPORT void JNICALL Java_org_doublesymmetry_pocketar_PocketARPlugin_nativeShutdown(JNIEnv *p_env, jobject) {
	if (owner) owner->_uninitialize();
	std::lock_guard<std::mutex> lock(bridge_mutex);
	bridge.resumed = false;
	bridge.state = 1;
	if (bridge.context) p_env->DeleteGlobalRef(bridge.context);
	bridge.context = nullptr;
}
}
