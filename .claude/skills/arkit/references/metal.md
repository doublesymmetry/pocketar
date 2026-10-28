# Metal camera ownership

Only two-plane full-range BT.601 is accepted. A missing matrix attachment keeps
the established BT.601 matrix (`420f` defaults to it) and increments
`color_tag_missing`; an explicitly different matrix or range is unsupported.
`CVMetalTextureCache` wraps R8 and RG8 planes without CPU image copies, then RD
imports and `Texture2DRD` expose them to the existing camera composite. The packet's
`imported_chroma` flag selects `.ra`; CPU-uploaded synthetic/ARCore frames keep `.rg`.
Unsupported colour encodings fail with a diagnostic instead of using a wrong matrix.

The texture pool owns every CVPixelBuffer and both CVMetalTextureRefs independently
of packet lifetime. A pair retires once no current, presented or ARView-applied
packet uses it, regardless of script-held Refs. On the render thread, clear/free the RS
wrappers first, then free the imported RD RIDs at swap count s. Release CoreVideo
owners at s+Q+1, where Q is `RenderingDevice.get_frame_delay()`: Godot 4.8 drops its
own import retain one swap after Q. Only `_end_frame`
increments the counter, as its first statement, after RD's actual swap. The pool
survives reset, uninitialize and interface recreation. Process shutdown stops
sessions before the pool releases owners; Godot's own import retains remain until
RD finalization. Never synchronously drain the GPU on reset.

Godot 4.7.2's same-format Metal import releases a texture it did not retain
(`drivers/metal/rendering_device_driver_metal.cpp:416`, `:527`).
A one-time same-format R8 import probe records CF retain-count delta, the native
pointer branch and engine version/hash. Only a missing engine retain enables one
compensating CFRetain per successful same-format import. Godot's deferred free
consumes that retain; the library must not balance it. Fixed 4.8 imports must
produce a positive delta and disable the workaround. Version strings alone do not
decide ownership. Device validation of this probe with Metal API Validation is
required. The flush policy is `CVMetalTextureCacheFlush(cache, 0)` after swaps that
release retired owners. At most seven camera buffers are held. Retaining packets
cannot pin those buffers. Retired Texture2DRD objects are cleared, never reused.

In debug builds, the retirement canary records each native texture's retain count
immediately after freeing imports and observes the first later swap where it drops.
Diagnostics report `retain_drop_swaps_min/max`; releasing native owners without an
observed drop increments `premature_release_count`. Retain counts are a heuristic:
in-flight command buffers may also own references. This does not prove GPU
completion. A separate check reads the background material's actual RS texture
parameters and fails on retired RIDs. `camera-image` also requires zero
`texture_refusals` and `buffer_drops`. A swap lasting at least 900ms
extends every pending native retirement by another Q swaps. Godot's Metal fence
wait still times out at 1s and ignores the failure: this mitigation cannot prove
safe GPU work beyond that engine limit. Run the long-stall validation on the phone.

For composition and Godot rendering source locations, read the
[rendering reference](../../scripted-sessions/references/rendering.md).
