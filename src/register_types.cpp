#include "core/handheld_ar.h"
#include "core/ar_plane_tracker.h"
#include "core/ar_raycast_hit.h"
#include "core/synthetic_ar_interface.h"
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#ifdef IOS_ENABLED
#include "platform/ios/arkit_interface.h"
#elif defined(ANDROID_ENABLED)
#include "platform/android/arcore_interface.h"
#endif

using namespace godot;

static HandheldAR *handheld_ar_singleton = nullptr;

static void initialize_handheld_ar(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	GDREGISTER_CLASS(ARFramePacket);
	GDREGISTER_CLASS(ARPlaneTracker);
	GDREGISTER_CLASS(ARRaycastHit);
	GDREGISTER_CLASS(HandheldAR);
	GDREGISTER_CLASS(SyntheticARInterface);
#ifdef IOS_ENABLED
	GDREGISTER_CLASS(ARKitInterface);
	GDREGISTER_INTERNAL_CLASS(ARKitTexturePool);
#elif defined(ANDROID_ENABLED)
	GDREGISTER_CLASS(ARCoreInterface);
#endif
	handheld_ar_singleton = memnew(HandheldAR);
	Engine::get_singleton()->register_singleton("HandheldAR", handheld_ar_singleton);
	UtilityFunctions::print("HANDHELD_AR LOADED ", handheld_ar_singleton->get_version(), " ", OS::get_singleton()->get_name());
}

static void uninitialize_handheld_ar(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
#ifdef IOS_ENABLED
		shutdown_arkit_interfaces();
		shutdown_arkit_texture_pool();
#endif
		Engine::get_singleton()->unregister_singleton("HandheldAR");
		memdelete(handheld_ar_singleton);
		handheld_ar_singleton = nullptr;
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
