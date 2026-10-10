#pragma once

#include <jni.h>
#include <string>
#include <vector>

namespace godot { class ARCoreInterface; }

struct ARCoreBridgeState {
	JavaVM *vm = nullptr;
	jobject activity = nullptr;
	jobject context = nullptr;
	int state = 1;
	bool resumed = false;
	int rotation = 0;
	int width = 1;
	int height = 1;
	uint64_t geometry_revision = 0;
	std::string apk_version;
	std::string detail;
	std::vector<int> state_history;
};

ARCoreBridgeState arcore_bridge_snapshot();
void arcore_bridge_set_owner(godot::ARCoreInterface *p_owner);

class ARCoreJNIEnv {
	JavaVM *vm;
	bool attached = false;
public:
	JNIEnv *env = nullptr;
	explicit ARCoreJNIEnv(JavaVM *p_vm);
	~ARCoreJNIEnv();
};
