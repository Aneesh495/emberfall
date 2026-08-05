# Build status

Emberfall runs as a native game and scene workshop. The current acceptance scope is a diagnostic preview. The full reference platform is Linux x86-64 with a dedicated Vulkan GPU. The available hardware for native rendering checks is an Apple M2 using MoltenVK.

| Gate | Full status | Available evidence or missing work |
| --- | --- | --- |
| G01 Original engine | Unverified | Original C++ world, simulation, editor, renderer, cook and transport paths are in use. A final source and dependency audit remains. |
| G02 Playable product | Unverified | All three levels replay through the standalone game. Both UDP test endpoints finish each level. A full interactive packaged solo and network session remains. |
| G03 ECS | Passed | One million seeded operations against an independent record model, stale entity checks and deferred removal. |
| G04 Scheduler | Unverified | Random graph and conflict checks, cancellation and four active workers. Broader race instrumentation remains. |
| G05 Rendering correctness | Unverified | Native synchronization validation and resize fixture pass for the implemented graph. Static material, local shadow and temporal history fixtures exercise the Vulkan path. Full reload and material validation remain. |
| G06 Rendering performance | Blocked | The M2 diagnostic uses the target geometry count and resolution with a narrower pass set. The Linux reference GPU and full graph are outstanding. |
| G07 ECS performance | Unverified | Separate one- and four-worker 100,000-entity diagnostic available. The reference CPU is not pinned. |
| G08 Physics | Unverified | Analytical linear contacts, momentum exchange, stack, tunneling, character steps and moving platforms are exercised. Convex and angular fixtures cover off-center impacts, rotated contacts and rolling. Mixed-shape manifolds, rotational sweeps and the complete reference workload remain. |
| G09 Content | Unverified | Catalog manifests, prefab rebuilds and scene cache reload are exercised. Static glTF and GLB meshes cook and render in both native programs. Texture and animation imports and GPU resource streaming remain. |
| G10 Editor | Unverified | 1,000 edit operations, recovery reopen and separate play state are checked. The full prefab override and editor integration matrix remains. |
| G11 Animation and navigation | Unverified | Weighted skin fixture, skinned cloth and collision-aware companion A* are available. Full animation asset imports and companion integration fixtures remain. |
| G12 Networking | Unverified | All levels finish across two loopback endpoints under simulated impairment, including reconnect and duplicate event checks. Packaged two-machine play remains. |
| G13 Replay and save | Unverified | Exact recorded command replay, revision checks, save migration and corrupt/truncated record rejection. Interrupted storage fault injection remains. |
| G14 Soak | Blocked | The full mixed eight-hour workload has not been run. |
| G15 Tooling | Unverified | Raw hardware timestamp and workload reports, scene inspector, and content manifest exist. Integrated render, physics and network inspectors remain. |
| G16 Semantic verifier | Unverified | The diagnostic verifier independently recomputes raw timing and rejects altered replay and duplicate events. Full coverage needs all mandatory workload artifacts. |
| G17 Platform and release | Blocked | The macOS arm64 package launches using bundled libraries. A clean Linux hardware release is outstanding. |
| G18 Completion | Blocked | Full acceptance and release remain open. |

The renderer currently uses an opaque instanced mesh path, three directional cascades and one six-face point shadow, tiled forward point lighting, and rigid-motion temporal anti-aliasing. Depth clustering, multi-light shadow allocation, normal-mapped PBR assets, particle and decal passes, and skinned velocity remain. The physics stress solver uses translating boxes and spheres. The solver also supports rotated convex shapes and opt-in angular response through its bounded single-witness path. The game controller has axis-aligned contacts and current-geometry navigation. The content pipeline cooks native scene and prefab data.
