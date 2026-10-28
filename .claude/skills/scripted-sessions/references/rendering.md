# Rendering constraints

Use both compositions for rendering changes. Public behavior is documented in
[composition](../../../../README.md#composition).

`IN_SCENE` is the lower-bandwidth composition for games that process the camera
and virtual objects together. An opaque, unshaded camera quad draws at exact
reverse-Z depth zero, with depth writes and the normal greater-or-equal test.
The XR viewport keeps `transparent_bg=true` to suppress Mobile's automatic fog
sky and uses `use_hdr_2d=false`; its texture is drawn without a canvas material.
The camera fills every sample with alpha one, so ordinary transparency and MSAA
blend in linear scene colour and keep the final alpha at one. Screen-reading
materials can sample the camera. Fog is disabled on the quad, but tonemapping,
exposure, colour adjustments and glow affect its pixels. Mobile's normalized
scene buffer also clips radiance above approximately two, including bright
virtual emission before glow or tonemapping.

The quad's culling AABB is a small box just beyond the calibrated near-plane
centre, updated with each packet; it remains inside asymmetric frusta and short
far planes. Occlusion culling and shadow casting are disabled. Material priority
127 is reserved for the camera: ordinary opaque scene materials must use lower
priorities to draw before it. Mobile compares `sort_key2` first, with priority in
its high bits; depth in `sort_key1` cannot guarantee late drawing. Equal-priority
materials are ordered by shader/material, and stencil opaques form a later group.
These exceptions affect early-depth savings; exact zero still preserves scene depth.

Godot 4.7.2 Mobile has no per-instance receive-decal switch or spatial render mode
that disables decals. It pairs decals by AABB overlap and shared visibility layer
masks, and processes them even in unshaded shaders. Keep decal boxes away from the
camera's near-plane proxy: an overlapping box that contains the camera can paint
the camera image. ARView reserves no additional visibility layer. Other cameras
and reflection probes sharing its private world can also see the quad when their
masks and frusta include it.

Enable `rendering/anti_aliasing/quality/use_debanding` for `IN_SCENE`. This dithers
materials before Mobile's ten-bit scene write. In the measured desktop
Mobile/Metal gradient, 39 of 255 adjacent source levels merged, with visible loss
in dark values below 32; material dithering kept column means within 0.44 of an
eight-bit level, at the cost of a few levels of per-pixel noise. Viewport-stage
debanding alone does not recover this precision. Camera equality within one
level is not the in-scene contract.

Use `ISOLATED_CAMERA` when a game's tonemapping, grading or glow must leave camera
pixels unchanged, or its virtual scene needs the float colour range. The camera
is an SDR canvas below an HDR virtual viewport. That viewport stores
premultiplied linear RGB; the final shader divides by coverage alpha, applies the
sRGB transfer function, then multiplies by alpha for `blend_premul_alpha` in the
SDR canvas. Plain `blend_mix` would multiply alpha twice. Screen-space refraction
sees only the virtual scene, and alpha-zero glow is discarded. The camera is
unaffected by the game's Environment, including fog. Keep the outer viewport SDR
in both modes. Both camera shaders share the same YCbCr/RGB sampling include.

Environment behaviour and limits follow [composition](../../../../README.md#composition).
ARView copies the non-background settings into a private Environment before each
draw, forces a transparent-black clear, and applies live edits even when the
camera packet repeats.

## Engine evidence

Paths below are relative to the Godot 4.7.2 source tree.

| Decision | Source |
| --- | --- |
| One XR view keeps canvas rendering | `servers/rendering/renderer_viewport.cpp:349` |
| XR renders with the interface transform and full projection | `servers/rendering/renderer_scene_cull.cpp:2749` |
| XR matrix array is copied column-major | `servers/xr/xr_interface_extension.cpp:223` |
| Projection has -Z forward and near/far NDC -1/+1 | `core/math/projection.cpp:364` (also check `godot-cpp/src/variant/projection.cpp:366`) |
| CPU RG8 uses R,G,0,1; imported RD RG8 uses R,R,R,G | `servers/rendering/renderer_rd/storage_rd/texture_storage.cpp:2296` and `:2871` |
| Shader mat3 uniforms consume a Basis | `servers/rendering/renderer_rd/storage_rd/material_storage.cpp:303` |
| Transparent viewport supplies transparent black | `servers/rendering/renderer_viewport.cpp:394` |
| Mobile writes scene colour and alpha, then blends RGB by source alpha | `servers/rendering/renderer_rd/shaders/forward_mobile/scene_forward_mobile.glsl:2344`; `servers/rendering/renderer_rd/storage_rd/material_storage.cpp:655` |
| HDR 2D forces float scene buffers, avoiding Mobile's two-bit alpha | `servers/rendering/renderer_rd/storage_rd/render_scene_buffers_rd.cpp:159`; `servers/rendering/renderer_rd/storage_rd/render_scene_buffers_rd.h:195` |
| HDR viewport skips the final sRGB conversion | `servers/rendering/renderer_rd/renderer_scene_render_rd.cpp:752` and `:961` |
| Tonemapping changes RGB and preserves alpha | `servers/rendering/renderer_rd/shaders/effects/tonemap.glsl:860`, `:893`, `:954` |
| Canvas uses the same blend factors, including premultiplied alpha | `servers/rendering/renderer_rd/renderer_canvas_render_rd.cpp:1517`; `servers/rendering/renderer_rd/storage_rd/material_storage.cpp:700` |
| Mobile sorts opaque priority before shader/material and depth; stencil uses a separate grouping | `servers/rendering/renderer_rd/forward_mobile/render_forward_mobile.h:339`, `:355`, `:484` |
| Transparent background suppresses Mobile's automatic fog sky | `servers/rendering/renderer_rd/forward_mobile/render_forward_mobile.cpp:1088` |
| Mobile decal pairing uses layer masks; decals run even for unshaded materials | `servers/rendering/renderer_scene_cull.cpp:244`; `servers/rendering/renderer_rd/shaders/forward_mobile/scene_forward_mobile.glsl:1474` |
| Mobile material dithering precedes normalized scene storage | `servers/rendering/renderer_rd/shaders/forward_mobile/scene_forward_mobile.glsl:2363` |
| Main-thread pre-draw precedes draw; post-draw follows it | `servers/rendering/rendering_server_default.cpp:446` and `:229` |

`display_transform` is this library's explicit convention, derived in
`src/core/projection.cpp`, not a promise that a future native API has the same
mapping direction. The native adapter must convert its UV and orientation
conventions before constructing a packet. The numerical tests compare against
an independent pixel-rotation/crop oracle.
