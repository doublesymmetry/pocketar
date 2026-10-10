#pragma once

#include <godot_cpp/classes/xr_interface_extension.hpp>
#include <godot_cpp/core/class_db.hpp>

namespace godot {

class ARKitInterface : public XRInterfaceExtension {
	GDCLASS(ARKitInterface, XRInterfaceExtension)

protected:
	static void _bind_methods() {
		ClassDB::bind_method(D_METHOD("get_availability"), &ARKitInterface::get_availability);
	}

public:
	String get_availability() const { return "unsupported"; }
	StringName _get_name() const override { return "ARKitInterface"; }
	bool _initialize() override { return false; }
	bool _is_initialized() const override { return false; }
};

}
