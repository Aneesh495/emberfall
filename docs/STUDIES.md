# Engineering studies

## Allocation and lifetime

The scene cache accounts for resident versions and weakly tracks versions pinned by readers after reload. The Vulkan frame slots have a separate memory owner and are reused only after their timeline values retire. Both domains bound allocations up front. Heap tracing and a long mixed soak remain open. [scene cache](../src/scene_stream.cpp), [Vulkan frame slots](../src/gpu/state.hpp)

## ECS layout

The archetype table keeps a contiguous column for each component in the signature. Swap removal makes erasure proportional to present component columns. The independent lifecycle fixture exercises migration, stale slots, matching queries and deferred destruction. [archetype table](../include/emberfall/world.hpp), [independent lifecycle fixture](../tests/world.cpp)

## Task scheduling

The scheduler computes transitive ancestry during validation. Read/read overlap is legal; an unordered shared write fails. The random graph fixture checks a sequential reference and uses a four-party latch to prove four bodies can run together. [scheduler](../src/scheduler.cpp), [random graph fixture](../tests/scheduler.cpp)

## Fixed-step timing

The application captures edge actions until a 60 Hz tick consumes them. An overload keeps at most five ticks of accumulated work. Timing pressure may slow simulation wall-clock progress, while replay follows the exact accepted commands. [The application](../src/native/game_main.cpp)

## Render graph and synchronization

The resource transitions prohibit sampling an unwritten image. Synchronization validation detected the missing acquire-stage dependency on an initial swapchain import. Adding the acquire stage to the source scope repaired the actual layout transition hazard. [resource transitions](../src/gpu/render_graph.hpp)

## Visibility and geometry budget

Visibility intersects an instance's eight transformed corners with homogeneous clip planes. A separate light frustum includes potential casters that the camera cannot see. The stress mesh intentionally subdivides its faces. The asset set currently uses one geometry type for general scene blocks. [Visibility](../src/gpu/pipeline.cpp)

## Materials and lighting

The lighting shader accumulates ambient, directional, emissive, and distance-bounded local light in linear HDR. The display shader applies the filmic approximation and edge filter before surface conversion. The bounded forward loop does more work with more lamps and has no clustered bins yet. [The lighting shader](../shaders/scene.frag), [display shader](../shaders/tonemap.frag)

## Content cooking

The recipe hashes exact sources and the local composer, then keys its cache by a scene's sorted dependencies and placements. The cooker fixture compares two clean manifests, changes one scene, changes a shared prefab, and rejects malformed data without replacing the manifest. [The recipe](../scripts/cook.py), [cooker fixture](../tests/cooker.py)

## Streaming and hot reload

Versioned scene loading stages parse work on two startup workers. An obsolete completion loses its generation match and cannot overwrite a new request. The reload fixture keeps a lease across a corrupt reload and a later valid replacement. [Versioned scene loading](../src/scene_stream.cpp), [reload fixture](../tests/stream.cpp)

## Contact solver

The translating rigid-body solver uses sweep-and-prune candidates and shape-specific contacts. It warm-starts normal and tangent impulses with ten velocity iterations and separates overlap with eight position iterations. A single position pass left excessive compression in a five-box stack. The revised solve also suppresses sleep while residual contact penetration exceeds its tolerance. [The translating rigid-body solver](../src/dynamics.cpp)

## Animation

The animation core samples strictly ordered keys, blends joint poses, walks a parent-first hierarchy, and applies inverse bind matrices. The cloth asset uses weighted skinning for the visible and shadow passes. Animation has no authority over player root motion. [The animation core](../src/animation.cpp)

## Navigation

The companion route samples support on a half-meter grid and uses a Manhattan heuristic. Swept edges reject barriers and check midpoint support. The fixtures compare a closed wall, a detour, and a direct path. The bounded representation assumes one near-horizontal working floor. [The companion route](../src/navigation.cpp), [fixtures](../tests/navigation.cpp)

## Editor command history

Document edits contain stable IDs and expected values. Applying, undoing and redoing all validate candidate state. A flat recovery journal restores dirty content after restart. Reopening the journal does not reproduce the pre-crash undo stack. [Document edits](../src/editor.cpp)

## Replication and reconciliation

Transport bounds packet length, fragment count, reassembly buckets, retries, and receive work per poll. Application input counters carry jump and use edges across snapshot loss. The client only predicts a bounded window and receives authoritative device and checkpoint state. [Transport](../src/net_session.cpp)

## Replay and persistence

The replay verifier checks every state hash from a known room revision and accepted input stream. Save migration changes a compact checkpoint record independently of replay format. The supported exact contract is the same executable build and architecture. [The replay verifier](../src/replay.cpp), [Save migration](../src/campaign.cpp)

## Game integration

The shipped routes require simultaneous plate states, physical line of sight to a receiver, and a ridden platform. The loopback run uses real UDP sockets for the remote actor, including a receiver interaction and reconnect between levels. Editor revisions and catalog placement run through the same validation and world construction used by the package. [The shipped routes](../tests/playthrough.cpp), [loopback run](../tests/network.cpp)