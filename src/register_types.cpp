#include "core/pocketar.h"
#include "core/ar_plane_tracker.h"
#include "core/ar_raycast_hit.h"
#include "core/synthetic_ar_interface.h"
#include "core/scripted_ar_interface.h"
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

static PocketAR *pocketar_singleton = nullptr;

static void initialize_pocketar(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	GDREGISTER_CLASS(ARFramePacket);
	GDREGISTER_CLASS(ARPlaneTracker);
	GDREGISTER_CLASS(ARRaycastHit);
	GDREGISTER_CLASS(PocketAR);
	GDREGISTER_ABSTRACT_CLASS(SimulatedARInterface);
	GDREGISTER_CLASS(SyntheticARInterface);
	GDREGISTER_CLASS(ScriptedARInterface);
#ifdef IOS_ENABLED
	GDREGISTER_CLASS(ARKitInterface);
	GDREGISTER_INTERNAL_CLASS(ARKitTexturePool);
#elif defined(ANDROID_ENABLED)
	GDREGISTER_CLASS(ARCoreInterface);
#endif
	pocketar_singleton = memnew(PocketAR);
	Engine::get_singleton()->register_singleton("PocketAR", pocketar_singleton);
	UtilityFunctions::print("POCKETAR LOADED ", pocketar_singleton->get_version(), " ", OS::get_singleton()->get_name());
}

static void uninitialize_pocketar(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
#ifdef IOS_ENABLED
		shutdown_arkit_interfaces();
		shutdown_arkit_texture_pool();
#endif
		Engine::get_singleton()->unregister_singleton("PocketAR");
		memdelete(pocketar_singleton);
		pocketar_singleton = nullptr;
	}
}

extern "C" {
GDExtensionBool GDE_EXPORT pocketar_library_init(GDExtensionInterfaceGetProcAddress p_get_proc_address, GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization) {
	GDExtensionBinding::InitObject init(p_get_proc_address, p_library, r_initialization);
	init.register_initializer(initialize_pocketar);
	init.register_terminator(uninitialize_pocketar);
	init.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
	return init.init();
}
}
