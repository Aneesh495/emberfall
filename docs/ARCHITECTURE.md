# Emberfall architecture

## Ownership and units

Coordinates are right-handed, +Y is up, and local -Z is forward. Distances use meters. Vulkan camera projection inverts clip Y and maps depth to [0,1]. The runtime advances a 60 Hz simulation and extracts position between previous and current states. Native event edges stay latched until the next tick, and catch-up is capped at five ticks. The app drops excess elapsed time after a stall.

Archetype columns hold live component data. Slots map an entity index and generation to a table row. Migration appends to the destination and then swap-removes from the old table, updating the moved entity's row. Structural edits are rejected during queries and can be queued for a barrier. Persistent scene IDs are independent of transient handles. [Archetype columns](../include/emberfall/world.hpp)

Schedules declare logical memory ranges as read or write. Ordered conflicts require a path in the DAG. Workers share a bounded ready queue and use a condition variable to establish ordering between producer completion and consumer start. Culling workers share read-only scene items and own disjoint ranges of a byte array. Frame command recording waits for that work. [Schedules](../src/scheduler.cpp)

## Asset to screen

The cooker uses a catalog of stable asset IDs, byte hashes, and explicit prefab placements. The composer remaps IDs and validates the final play scene with the same parser and constructors used in the game. The game requests all required scenes in priority order from the scene cache. Each committed version is immutable while leased. Missing campaign assets prevent the game from starting. [Cooking dependencies](../scripts/cook.py), [composer](../src/tools/main.cpp), [scene cache](../src/scene_stream.cpp)

Game extraction produces affine instance transforms and material tint. Skin extraction evaluates weighted vertices against the animated joint palette and writes separate dynamic meshes. The frame recorder runs light-frustum and camera-frustum culling, renders three directional cascades and six point-light faces into a D32 atlas, draws instanced opaque geometry and skinned meshes into RGBA16F, and applies exposure, tone mapping, edge filtering, and overlay to the swapchain. The graph declares depth writes, fragment reads, color writes, optional readback, and presentation. [Game extraction](../src/game.cpp), [Skin extraction](../src/scarf.cpp), [frame recorder](../src/gpu/frame.cpp), [graph](../src/gpu/render_graph.hpp)

A device timeline gates reuse of mapped uploads, query pools, and offscreen images. Per-image present semaphores remain associated with swapchain images until acquisition establishes reuse. The acquire wait targets color output. The first backbuffer layout transition imports that stage as its source, even when discarding previous color contents. Reused offscreen images import the previous shader or depth access scope. Linux validation exposed that a host timeline wait alone did not establish those queue dependencies. Resize drains device work before recreating image-sized resources. Frame query samples identify the older frame to which they belong.

## Input to physics to network

Platform input maps SDL events and axes into edge and held states. Game ticks sample pressure and light circuits, move devices, rebuild the collider broad phase, carry supported actors, sweep character commands, apply interactions in slot order, and emit lifecycle events. Simulation advances only on the host during authoritative network play. The client predicts for its own actor and replays at most 48 pending commands after an acknowledged input order. [Platform input](../src/native/platform.cpp), [Game ticks](../src/game.cpp)

The UDP session separates packet acknowledgements from application input counters. A packet carries a token, sequence, highest observed sequence, and previous-32 acknowledgement mask. Hello and welcome retry within fixed time and queue limits. A snapshot uses at most eight fragments per message with at most four in-flight reassembly buckets. The receiver accepts only the negotiated peer, schema, and level revision. Movement axes and snapshots are quantized. Client prediction can recompute transient device poses, but completion and checkpoint presentation derive from the authoritative snapshot. Cross-platform simulation hashes are outside the replay contract. [UDP session](../src/net_session.cpp)

## Editor command to save

Scene transactions use stable node identities and the expected before state. The editor builds a candidate scene, validates transforms and references, and publishes a transaction only if all edits succeed. Undo applies inverse node changes through the same validator. Play creates a separate runtime from the edit scene. Editor-only fields and operation history do not enter the play save. [Scene transactions](../src/editor.cpp)

Atomic publishing writes a same-directory temporary file, flushes it, renames it, and flushes the containing directory. The recovery journal stores the base scene fingerprint and the pending canonical scene. If the authoring process exits before saving, reopening with the same base restores the dirty edit as a transaction. The editor reports a conflict if the base changed externally. [Atomic publishing](../src/files.cpp)

## Save and replay recovery

Progress contains a level index, checkpoint ID, and completion bit scoped to the cooked campaign revision. Loading restarts the room's mechanisms and places both actors at the recorded safe beacon. Version 1 saves migrate by adding a default completion bit. A corrupt save produces visible feedback and leaves the original file available until a new checkpoint is saved. Replay stores the effective pair of commands and the complete declared simulation hash for each tick, including commands synthesized for the companion or accepted from the network host. The verifier re-executes those ticks with the exact recorded executable build and checks each hash. [Progress](../src/campaign.cpp), [Replay](../src/replay.cpp)