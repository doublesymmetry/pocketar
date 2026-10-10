#include "core/handheld_ar.h"
#include "core/synthetic_ar_interface.h"
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#ifdef IOS_ENABLED
#include "platform/ios/interface_stub.h"
#elif defined(ANDROID_ENABLED)
#include "platform/android/interface_stub.h"
#endif

using namespace godot;

static HandheldAR *handheld_ar = nullptr;

static void initialize_handheld_ar(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	GDREGISTER_CLASS(ARFramePacket);
	GDREGISTER_CLASS(HandheldAR);
	GDREGISTER_CLASS(SyntheticARInterface);
#ifdef IOS_ENABLED
	GDREGISTER_CLASS(ARKitInterface);
#elif defined(ANDROID_ENABLED)
	GDREGISTER_CLASS(ARCoreInterface);
#endif
	handheld_ar = memnew(HandheldAR);
	Engine::get_singleton()->register_singleton("HandheldAR", handheld_ar);
	UtilityFunctions::print("HANDHELD_AR LOADED ", handheld_ar->get_version(), " ", OS::get_singleton()->get_name());
}

static void uninitialize_handheld_ar(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		Engine::get_singleton()->unregister_singleton("HandheldAR");
		memdelete(handheld_ar);
		handheld_ar = nullptr;
	}
}

extern "C" {
GDExtensionBool GDE_EXPORT handheld_ar_library_init(GDExtensionInterfaceGetProcAddress p_get_proc_address, GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization) {
	GDExtensionBinding::InitObject init(p_get_proc_address, p_library, r_initialization);
	init.register_initializer(initialize_handheld_ar);
	init.register_terminator(uninitialize_handheld_ar);
	init.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
	return init.init();
}
}
