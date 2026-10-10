#pragma once

#include "core/ar_frame_packet.h"
#include <godot_cpp/classes/texture2drd.hpp>
#include <godot_cpp/classes/xr_interface_extension.hpp>
#include <memory>

namespace godot {

class ARKitTexturePool : public XRInterfaceExtension {
	GDCLASS(ARKitTexturePool, XRInterfaceExtension)
	struct Impl;
	std::unique_ptr<Impl> impl;
	uint64_t rd_swaps = 0;

protected:
	static void _bind_methods() {}

public:
	struct Import {
		Ref<Texture2DRD> y, cbcr;
		ARFramePacket::ColorEncoding color = ARFramePacket::BT601_FULL;
	};
	ARKitTexturePool();
	~ARKitTexturePool();
	bool setup();
	bool import_image(void *p_pixel_buffer, Import &r_import);
	String get_import_error() const;
	bool check_sample(const Ref<ARFramePacket> &p_packet);
	Dictionary get_diagnostics() const;
	void shutdown();
	StringName _get_name() const override { return "HandheldARKitRetirement"; }
	uint32_t _get_capabilities() const override { return 0; }
	uint32_t _get_view_count() override { return 0; }
	bool _is_initialized() const override;
	void _pre_render() override;
	void _end_frame() override;
};

Ref<ARKitTexturePool> arkit_texture_pool();
void shutdown_arkit_texture_pool();

}
