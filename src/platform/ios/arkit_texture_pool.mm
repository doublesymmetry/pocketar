#include "arkit_texture_pool.h"
#include "import_retirement.h"
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/xr_server.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#import <CoreVideo/CoreVideo.h>
#import <Metal/Metal.h>
#include <algorithm>
#include <unordered_set>
#include <vector>

#if !__has_feature(objc_arc)
#error ARKit requires Objective-C ARC
#endif

namespace godot {

struct ARKitTexturePool::Impl {
	struct Entry {
		CVPixelBufferRef buffer = nullptr;
		CVMetalTextureRef native[2] = { nullptr, nullptr };
		RID rd[2];
		Ref<Texture2DRD> texture[2];
		handheld_ar::ImportRetirement lifetime;
		uint64_t free_usec = 0;
#ifdef DEBUG_ENABLED
		handheld_ar::ImportRetainCanary retain_canary[2];
#endif
		bool matches(const Ref<ARFramePacket> &p_packet) const {
			return p_packet.is_valid() && texture[0] == p_packet->get_y_texture() && texture[1] == p_packet->get_cbcr_texture();
		}
	};
	RenderingDevice *rd = nullptr;
	CVMetalTextureCacheRef cache = nullptr;
	std::vector<Entry> entries;
	bool ready = false;
	bool engine_retains = false;
	uint32_t delay = 0;
	uint64_t draw_started = 0;
	Dictionary diagnostics;
	String import_error;
	std::vector<double> import_times, retire_times;
	size_t import_cursor = 0, retire_cursor = 0;
#ifdef DEBUG_ENABLED
	std::unordered_set<uint64_t> retired_rs_rids;
	RID camera_material;
#endif

	void increment(const char *p_key) { diagnostics[p_key] = int64_t(diagnostics.get(p_key, 0)) + 1; }
	static void sample(std::vector<double> &p_samples, size_t &p_cursor, double p_value) {
		if (p_samples.size() < 131072) p_samples.push_back(p_value);
		else { p_samples[p_cursor] = p_value; p_cursor = (p_cursor + 1) % p_samples.size(); }
	}
	static double percentile(std::vector<double> p_samples, double p_fraction) {
		if (p_samples.empty()) return 0;
		const size_t index = size_t((p_samples.size() - 1) * p_fraction);
		std::nth_element(p_samples.begin(), p_samples.begin() + index, p_samples.end());
		return p_samples[index];
	}
	void free_imports(Entry &p_entry, uint64_t p_swap) {
		for (int plane = 0; plane < 2; ++plane) {
			// Texture2DRD owns the RS shared view, which must disappear before its RD source.
			if (p_entry.texture[plane].is_valid()) {
#ifdef DEBUG_ENABLED
				retired_rs_rids.insert(p_entry.texture[plane]->get_rid().get_id());
#endif
				p_entry.texture[plane]->set_texture_rd_rid(RID());
			}
			p_entry.texture[plane].unref();
			if (p_entry.rd[plane].is_valid()) rd->free_rid(p_entry.rd[plane]);
		}
		p_entry.lifetime.retire(p_swap, delay);
#ifdef DEBUG_ENABLED
		for (int plane = 0; plane < 2; ++plane) {
			if (p_entry.rd[plane].is_valid() && p_entry.native[plane]) {
				p_entry.retain_canary[plane].record_free(CFGetRetainCount((__bridge CFTypeRef)CVMetalTextureGetTexture(p_entry.native[plane])));
			}
		}
#endif
		p_entry.free_usec = Time::get_singleton()->get_ticks_usec();
		increment("retirement_count");
	}
	static void release_native(Entry &p_entry) {
		for (CVMetalTextureRef &texture : p_entry.native) {
			if (texture) CFRelease(texture);
			texture = nullptr;
		}
		if (p_entry.buffer) CVPixelBufferRelease(p_entry.buffer);
		p_entry.buffer = nullptr;
	}
};

static Ref<ARKitTexturePool> shared_pool;

ARKitTexturePool::ARKitTexturePool() : impl(new Impl) {}
ARKitTexturePool::~ARKitTexturePool() { shutdown(); }
bool ARKitTexturePool::_is_initialized() const { return impl->ready; }

Ref<ARKitTexturePool> arkit_texture_pool() {
	if (shared_pool.is_null()) {
		Ref<ARKitTexturePool> candidate;
		candidate.instantiate();
		if (!candidate->setup()) return Ref<ARKitTexturePool>();
		shared_pool = candidate;
		XRServer::get_singleton()->add_interface(shared_pool);
	}
	return shared_pool;
}

void shutdown_arkit_texture_pool() {
	if (shared_pool.is_null()) return;
	shared_pool->shutdown();
	XRServer::get_singleton()->remove_interface(shared_pool);
	shared_pool.unref();
}

bool ARKitTexturePool::setup() {
	ERR_FAIL_COND_V(!RenderingServer::get_singleton()->is_on_render_thread(), false);
	impl->rd = RenderingServer::get_singleton()->get_rendering_device();
	ERR_FAIL_NULL_V(impl->rd, false);
	impl->delay = impl->rd->get_frame_delay();
	id<MTLDevice> device = (__bridge id<MTLDevice>)(void *)impl->rd->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_LOGICAL_DEVICE, RID(), 0);
	ERR_FAIL_COND_V(!device, false);
	MTLTextureDescriptor *descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm width:4 height:4 mipmapped:NO];
	descriptor.storageMode = MTLStorageModePrivate;
	descriptor.usage = MTLTextureUsageShaderRead;
	id<MTLTexture> __attribute__((objc_precise_lifetime)) probe = [device newTextureWithDescriptor:descriptor];
	ERR_FAIL_COND_V(!probe, false);
	const CFIndex before = CFGetRetainCount((__bridge CFTypeRef)probe);
	RID rid = impl->rd->texture_create_from_extension(RenderingDevice::TEXTURE_TYPE_2D, RenderingDevice::DATA_FORMAT_R8_UNORM,
		RenderingDevice::TEXTURE_SAMPLES_1, RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT, (uint64_t)(__bridge void *)probe, 4, 4, 1, 1, 1);
	ERR_FAIL_COND_V(!rid.is_valid(), false);
	const CFIndex after = CFGetRetainCount((__bridge CFTypeRef)probe);
	const bool same = impl->rd->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_TEXTURE, rid, 0) == (uint64_t)(__bridge void *)probe;
	impl->engine_retains = after - before >= 1;
	if (same && !impl->engine_retains) CFRetain((__bridge CFTypeRef)probe);
	impl->rd->free_rid(rid);
	ERR_FAIL_COND_V_MSG(!same, false, "ARKit Metal retain probe did not take the same-format import branch.");
	if (CVMetalTextureCacheCreate(kCFAllocatorDefault, nullptr, device, nullptr, &impl->cache) != kCVReturnSuccess) return false;
	impl->diagnostics["retain_probe_delta"] = int64_t(after - before);
	impl->diagnostics["retain_probe_same_branch"] = same;
	impl->diagnostics["retain_workaround_active"] = !impl->engine_retains;
	impl->diagnostics["engine_version"] = Engine::get_singleton()->get_version_info();
	impl->diagnostics["flush_policy"] = "after swaps that release retired native owners";
	impl->diagnostics["camera_path"] = "cvmetaltexturecache_texture2drd";
	impl->diagnostics["buffer_limit"] = 7;
	impl->diagnostics["frame_delay"] = int(impl->delay);
#ifdef DEBUG_ENABLED
	impl->diagnostics["retirement_canary_enabled"] = true;
#else
	impl->diagnostics["retirement_canary_enabled"] = false;
#endif
	impl->diagnostics["retirement_canary_kind"] = "native retain-count drop heuristic";
	impl->diagnostics["last_import_error"] = String();
	impl->diagnostics["retain_drop_swaps_min"] = int64_t(-1);
	impl->diagnostics["retain_drop_swaps_max"] = int64_t(-1);
	for (const char *key : { "imports", "retirement_count", "native_release_count", "buffer_drops", "buffer_high_water", "premature_release_count", "sample_after_free_count", "sample_checks", "material_sample_checks", "retain_drop_count", "same_branch_failures", "long_swap_count", "color_tag_missing" }) impl->diagnostics[key] = int64_t(0);
	impl->ready = true;
	UtilityFunctions::print("ARKIT Metal retain probe ", impl->diagnostics);
	return true;
}

bool ARKitTexturePool::import_image(void *p_pixel_buffer, Import &r_import) {
	ERR_FAIL_COND_V(!impl->ready || !RenderingServer::get_singleton()->is_on_render_thread(), false);
	impl->import_error = String();
	if (impl->entries.size() >= 7) {
		impl->increment("buffer_drops");
		impl->diagnostics["last_import_error"] = "camera buffer limit reached";
		return false;
	}
	CVPixelBufferRef buffer = (CVPixelBufferRef)p_pixel_buffer;
	const OSType format = CVPixelBufferGetPixelFormatType(buffer);
	CFTypeRef matrix = CVBufferCopyAttachment(buffer, kCVImageBufferYCbCrMatrixKey, nullptr);
	const bool full_601 = format == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange && (!matrix || CFEqual(matrix, kCVImageBufferYCbCrMatrix_ITU_R_601_4));
	if (!matrix) impl->increment("color_tag_missing");
	impl->diagnostics["pixel_format"] = int64_t(format);
	impl->diagnostics["ycbcr_matrix"] = matrix ? String([(NSString *)CFBridgingRelease(matrix) UTF8String]) : String("missing");
	for (const auto &attachment : { std::pair<CFStringRef, const char *>(kCVImageBufferColorPrimariesKey, "color_primaries"), { kCVImageBufferTransferFunctionKey, "transfer_function" } }) {
		CFTypeRef value = CVBufferCopyAttachment(buffer, attachment.first, nullptr);
		impl->diagnostics[attachment.second] = value ? String([(NSString *)CFBridgingRelease(value) UTF8String]) : String("missing");
	}
	if (!full_601 || CVPixelBufferGetPlaneCount(buffer) != 2) {
		impl->import_error = "ARKit requires two-plane 420f BT.601; an explicit different matrix or range is unsupported";
		impl->diagnostics["last_import_error"] = impl->import_error;
		return false;
	}
	const uint64_t started = Time::get_singleton()->get_ticks_usec();
	Impl::Entry entry;
	entry.buffer = CVPixelBufferRetain(buffer);
	bool ok = true;
	for (int plane = 0; plane < 2; ++plane) {
		const size_t width = CVPixelBufferGetWidthOfPlane(buffer, plane), height = CVPixelBufferGetHeightOfPlane(buffer, plane);
		const MTLPixelFormat metal_format = plane == 0 ? MTLPixelFormatR8Unorm : MTLPixelFormatRG8Unorm;
		if (CVMetalTextureCacheCreateTextureFromImage(kCFAllocatorDefault, impl->cache, buffer, nullptr, metal_format, width, height, plane, &entry.native[plane]) != kCVReturnSuccess) { ok = false; break; }
		id<MTLTexture> texture = CVMetalTextureGetTexture(entry.native[plane]);
		if (!texture || texture.pixelFormat != metal_format) { ok = false; break; }
		entry.rd[plane] = impl->rd->texture_create_from_extension(RenderingDevice::TEXTURE_TYPE_2D,
			plane == 0 ? RenderingDevice::DATA_FORMAT_R8_UNORM : RenderingDevice::DATA_FORMAT_R8G8_UNORM,
			RenderingDevice::TEXTURE_SAMPLES_1, RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT | RenderingDevice::TEXTURE_USAGE_CAN_COPY_FROM_BIT,
			(uint64_t)(__bridge void *)texture, width, height, 1, 1, 1);
		if (!entry.rd[plane].is_valid()) { ok = false; break; }
		const bool same = impl->rd->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_TEXTURE, entry.rd[plane], 0) == (uint64_t)(__bridge void *)texture;
		// The engine consumes this +1 at deferred texture_free on affected same-format imports.
		if (same && !impl->engine_retains) CFRetain((__bridge CFTypeRef)texture);
		if (!same) { impl->increment("same_branch_failures"); ok = false; break; }
		entry.texture[plane].instantiate();
		entry.texture[plane]->set_texture_rd_rid(entry.rd[plane]);
	}
	if (ok) {
		r_import.y = entry.texture[0];
		r_import.cbcr = entry.texture[1];
		r_import.color = ARFramePacket::BT601_FULL;
		impl->increment("imports");
	} else {
		impl->diagnostics["last_import_error"] = "CVMetalTexture or RenderingDevice import failed";
		impl->free_imports(entry, rd_swaps);
	}
	impl->entries.push_back(std::move(entry));
	impl->diagnostics["buffer_high_water"] = std::max(int64_t(impl->entries.size()), int64_t(impl->diagnostics["buffer_high_water"]));
	Impl::sample(impl->import_times, impl->import_cursor, double(Time::get_singleton()->get_ticks_usec() - started));
	return ok;
}

String ARKitTexturePool::get_import_error() const { return impl->import_error; }

void ARKitTexturePool::set_active_packets(const Ref<ARFramePacket> &p_current, const Ref<ARFramePacket> &p_presented, const Ref<ARFramePacket> &p_applied) {
	for (auto &entry : impl->entries) {
		entry.lifetime.set_active(entry.matches(p_current), entry.matches(p_presented), entry.matches(p_applied));
	}
}

void ARKitTexturePool::clear_active_packets() {
	set_active_packets(Ref<ARFramePacket>(), Ref<ARFramePacket>(), Ref<ARFramePacket>());
}

void ARKitTexturePool::_pre_render() {
	impl->draw_started = Time::get_singleton()->get_ticks_usec();
	for (auto &entry : impl->entries) {
		if (entry.lifetime.retirement_ready()) impl->free_imports(entry, rd_swaps);
	}
#ifdef DEBUG_ENABLED
	check_material(impl->camera_material);
#endif
}

void ARKitTexturePool::_end_frame() {
	++rd_swaps;
	const uint64_t now = Time::get_singleton()->get_ticks_usec();
	if (impl->draw_started && now - impl->draw_started >= 900000) {
		impl->increment("long_swap_count");
		for (auto &entry : impl->entries) entry.lifetime.defer_after_stall(rd_swaps, impl->delay);
	}
	bool released = false;
	for (auto it = impl->entries.begin(); it != impl->entries.end();) {
#ifdef DEBUG_ENABLED
		for (int plane = 0; plane < 2; ++plane) {
			auto &canary = it->retain_canary[plane];
			if (canary.armed && canary.observe(CFGetRetainCount((__bridge CFTypeRef)CVMetalTextureGetTexture(it->native[plane])), rd_swaps)) {
				const int64_t swaps = int64_t(canary.drop_swap - it->lifetime.free_swap);
				const int64_t previous_min = impl->diagnostics["retain_drop_swaps_min"];
				impl->diagnostics["retain_drop_swaps_min"] = previous_min < 0 ? swaps : std::min(previous_min, swaps);
				impl->diagnostics["retain_drop_swaps_max"] = std::max(int64_t(impl->diagnostics["retain_drop_swaps_max"]), swaps);
				impl->increment("retain_drop_count");
			}
		}
#endif
		if (!it->lifetime.release_ready(rd_swaps)) { ++it; continue; }
#ifdef DEBUG_ENABLED
		for (const auto &canary : it->retain_canary) {
			if (canary.armed && !canary.drop_observed) impl->increment("premature_release_count");
		}
#endif
		Impl::sample(impl->retire_times, impl->retire_cursor, double(now - it->free_usec));
		Impl::release_native(*it);
		impl->increment("native_release_count");
		it = impl->entries.erase(it);
		released = true;
	}
	if (released) CVMetalTextureCacheFlush(impl->cache, 0);
}

bool ARKitTexturePool::check_sample(const Ref<ARFramePacket> &p_packet) {
#ifdef DEBUG_ENABLED
	impl->increment("sample_checks");
	for (const auto &entry : impl->entries) {
		if (entry.matches(p_packet) && entry.lifetime.can_sample() &&
			impl->rd->texture_is_valid(entry.rd[0]) && impl->rd->texture_is_valid(entry.rd[1])) return true;
	}
	impl->increment("sample_after_free_count");
	return false;
#else
	return true;
#endif
}

void ARKitTexturePool::set_camera_material(const RID &p_material) {
#ifdef DEBUG_ENABLED
	impl->camera_material = p_material;
#endif
}

bool ARKitTexturePool::check_material(const RID &p_material) {
#ifdef DEBUG_ENABLED
	if (!p_material.is_valid()) return true;
	impl->increment("material_sample_checks");
	for (const char *parameter : { "y_texture", "cbcr_texture" }) {
		Variant value = RenderingServer::get_singleton()->material_get_param(p_material, parameter);
		if (value.get_type() == Variant::RID && impl->retired_rs_rids.count(RID(value).get_id())) {
			impl->increment("sample_after_free_count");
			return false;
		}
	}
#endif
	return true;
}

Dictionary ARKitTexturePool::get_diagnostics() const {
	Dictionary result = impl->diagnostics.duplicate(true);
	result["rd_swaps"] = int64_t(rd_swaps);
	result["held_buffers"] = int64_t(impl->entries.size());
	result["import_p50_us"] = Impl::percentile(impl->import_times, 0.5);
	result["import_p95_us"] = Impl::percentile(impl->import_times, 0.95);
	result["retire_p50_us"] = Impl::percentile(impl->retire_times, 0.5);
	result["retire_p95_us"] = Impl::percentile(impl->retire_times, 0.95);
	return result;
}

void ARKitTexturePool::shutdown() {
	if (!impl->ready) return;
	// Only extension shutdown calls this: sessions have stopped and RD finalization follows.
	for (auto &entry : impl->entries) {
		if (!entry.lifetime.freed) impl->free_imports(entry, rd_swaps);
		Impl::release_native(entry);
	}
	impl->entries.clear();
	CFRelease(impl->cache);
	impl->cache = nullptr;
	impl->ready = false;
}

}
