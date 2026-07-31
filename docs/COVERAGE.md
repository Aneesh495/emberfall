# Capability coverage

The source paths below identify the current implementation or diagnostic for each capability. The [gate matrix](BUILD_STATUS.md) tracks full acceptance.

| ID | Current implementation and boundary |
| --- | --- |
| E01 | SDL keyboard, one active gamepad, focus, resize, display changes, fullscreen and orderly teardown. The native diagnostic covers keyboard and focus. [Platform](../src/native/platform.cpp) |
| E02 | Entity and scene handle lifetime, bounded queues and frame resources, address and undefined-behavior checks. Allocation tracing and long-run leak certification remain. [Frame ownership](../src/gpu/state.hpp) |
| E03 | Generation-checked archetype ECS, columns, queries and deferred mutations. [Independent oracle](../tests/world.cpp) |
| E04 | Canonical hierarchy validation and affine transforms including negative and nonuniform scales. Preserve-world reparenting and per-node dirty propagation remain. [Scene transforms](../src/scene.cpp) |
| E05 | Bounded workers, DAG ordering, read/write validation, exception propagation and cancellation. [Scheduler](../src/scheduler.cpp) |
| E06 | Fixed-step accumulation and position interpolation, overload cap, native pause and editor single-step. Tick edges are captured. Per-command wall-clock timestamps remain. [Game loop](../src/native/game_main.cpp) |
| E07 | Versioned catalog, stable GUIDs, SHA-256 lineage and dependency-aware incremental cooking. [Catalog cooker](../scripts/cook.py) |
| E08 | glTF and GLB static geometry, node transforms and opaque metallic/roughness factors reach the game and editor. Texture, skin and clip imports remain. [Importer](GLTF.md) |
| E09 | Bounded asynchronous scene loading, priority, cancellation and lease-safe reload. GPU residency and render-asset streaming remain. [Scene stream](../src/scene_stream.cpp) |
| E10 | Canonical scene schema, hierarchy, prefab ID rebinding and editor round trips. Live inheritance, override migration and scene schema migrations remain. [Editor document](../src/editor.cpp) |
| E11 | Explicit image access, layouts, timeline ownership and slot reuse. Transient aliasing and integrated graph inspection remain. [Render graph](../src/gpu/render_graph.hpp) |
| E12 | CPU frustum tests, instance batching and separate shadow culling. Occlusion and runtime LOD remain. [Visibility](../src/gpu/pipeline.cpp) |
| E13 | Linear HDR color, filmic display and sRGB output. Metallic/roughness, image-based lighting and normal-mapped assets remain. [Display shader](../shaders/tonemap.frag) |
| E14 | Three directional cascades and one six-face point shadow fit a shared atlas. CPU-built forward light tiles bound fragment work. Depth clustering and multi-light shadow allocation remain. [Lighting shader](../shaders/scene.frag) |
| E15 | HDR, exposure and a spatial edge filter. Motion vectors and temporal history remain. [Display shader](../shaders/tonemap.frag) |
| E16 | Distance fog and editor gizmo geometry. Particles, decals and bounded translucent scene rendering remain. [Lighting shader](../shaders/scene.frag) |
| E17 | Device identity, shader load validation, raw GPU timestamps and framebuffer readback. Integrated pass and resource inspection remain. [Frame recorder](../src/gpu/frame.cpp) |
| E18 | Boxes, spheres, capsules and validated convex hulls with angular impulses. Single-witness contacts and bounded continuous sweeps. [Angular physics](ANGULAR_PHYSICS.md) |
| E19 | Warm starts, friction, restitution, sleeping, layer masks, triggers and conservative CCD against static boxes. [Dynamics fixtures](../tests/dynamics.cpp) |
| E20 | Swept character motion, small steps, grounded jumping, crouch and platform carry. General slopes remain. [Character controller](../src/collision.cpp) |
| E21 | Ordered joint tracks, interpolation and blending with weighted cloth skinning in the visible and shadow passes. Imported character rigs and a general animation state machine remain. [Animation](../src/animation.cpp) |
| E22 | Bounded ground A* replans through current collision geometry. Follow and hold modes drive the companion. [Navigation](../src/navigation.cpp) |
| E23 | Two ordered command slots and tick-scoped game events. A general extensible event subscription layer remains. [Game runtime](../src/game.cpp) |
| E24 | Pressure relays, timed doors, light routing, crate pushes, ferry sequencing and checkpoint recovery. [Route tests](../tests/playthrough.cpp) |
| E25 | Bounded procedural stereo cues with attenuation, panning and persisted volume. Streamed audio assets, bus mixing and occlusion remain. [Audio](../src/native/audio.cpp) |
| E26 | Title, pause, settings and ending UI with keyboard and gamepad mapping, text feedback and high contrast. [Native UI](../src/native/game_main.cpp) |
| E27 | Native viewport, picking, inspector, node list, prefab browser, multi-selection and translation gizmos. Rotation and scale gizmos remain. [Workshop](../src/native/editor_main.cpp) |
| E28 | Validated grouped node edits, undo, redo and flattened crash recovery. Serialization of full historical command stacks remains. [Editor history test](../tests/editor.cpp) |
| E29 | Separate play world with stop, pause and single-step. [Workshop](../src/native/editor_main.cpp) |
| E30 | Prefab creation, remapped instantiation, dirty state, recovery and content validation. Drag-and-drop prefab overrides remain. [Prefab implementation](../src/editor.cpp) |
| E31 | Versioned UDP packets, sequence/acknowledgement window, bounded handshake retries and fragment reassembly. Adaptive congestion control remains. [UDP session](../src/net_session.cpp) |
| E32 | Authoritative host, two slots, quantized snapshots, client interpolation and bounded prediction. Area-of-interest policies remain. [Client runtime](../src/native/game_main.cpp) |
| E33 | Prediction replays local commands after a bounded snapshot correction. Completion is read from authoritative state. Full irreversible event replication remains. [Snapshot application](../src/game.cpp) |
| E34 | Peer timeout, late snapshots, reconnection and seeded fault injection. [UDP fault test](../tests/network.cpp) |
| E35 | Atomic checkpoint saves, revision validation, migration and corrupt record checks. Interruption and disk-fault injection remain. [Save tests](../tests/campaign.cpp) |
| E36 | Recorded command pairs, state hashes and first-divergence reporting. [Replay](../src/replay.cpp) |
| E37 | Standalone packages exclude editor and build tools. macOS bundles its Vulkan runtime. Linux uses its system loader and driver. [Packaging](../scripts/package.sh) |
| E38 | Three complete scripted routes with distinct dependencies and a final state. Full interactive two-machine playthrough remains. [Route tests](../tests/playthrough.cpp) |
| E39 | Separate renderer, ECS, solver, asset cache and UDP diagnostics. The full reference workloads remain. [Experiment plan](EXPERIMENT_PLAN.md) |
| E40 | Source-linked execution and recovery walkthroughs. [Architecture](ARCHITECTURE.md) |
