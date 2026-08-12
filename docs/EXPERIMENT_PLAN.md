# Experiment plan

## Portability diagnostic v2

The render workload extends the shadow-atlas baseline from `m2-atlas-v1`. The new run includes a temporal resolve, a second opaque color output for motion and depth, and two animated eight-triangle cloth batches driven by the game animation path. Preserve both campaigns because the GPU pass set changed.

The local candidate is an Apple M2 with 16 GiB RAM and 10 GPU cores on macOS 26.7.2 (25G320). Vulkan observations use MoltenVK 1.4.2, Vulkan loader and validation layers 1.4.363, and glslang 16.6.0. Use a Release build with Apple Clang 21.0.0. Run timing workloads separately on an otherwise idle system. Retain failed runs and mark the profile as a portability diagnostic.

| Question | Unit and procedure | Decision rule |
| --- | --- | --- |
| Does archetype migration preserve component identity? | One million seeded operations against an independent row model, seed 50419 | Zero mismatches or stale handle acceptance |
| Can dependency declarations protect concurrent work? | 160 generated 80-task graphs, seed 73822, and conflict/cancellation probes | Exact outputs, conflicts rejected, at least four simultaneously active tasks in the concurrency probe |
| Does useful ECS work benefit from multiple workers? | 100,000 active entities, four component reads and three writes per entity per tick, one and four workers. Retain 200 ticks each after 40 warm-up ticks. | Identical terminal hashes. Report nearest-rank p95, compare against 8 ms diagnostic budget. |
| Do contacts stay finite at scale? | 1,000 dynamic boxes and spheres on a static floor, 60 Hz, 600 ticks, 60 warm-up ticks, fixed impulses every 60 ticks. | Zero nonfinite states or bodies below the floor. Compare nearest-rank p95 with 16.67 ms. |
| What do the GPU passes cost at high instance count? | 10,500 visible opaque instances using synthetic subdivided cube geometry, two animated cloth meshes and 2,016,016 submitted triangles. Use 128 point lights, three directional and six point-light tiles in a 2304-square atlas, HDR, temporal resolve and tone mapping at 1920x1080. Retain frames 19 through 158. | Check workload and five ordered hardware timestamps, shadow draw counts, tracked instances and animated triangle counts. Report p50 and p99 for GPU and host times. This scene lacks the complete required render feature set. |
| Does swapchain recreation preserve graph dependencies? | 120-frame native resize test with Vulkan and synchronization validation | Zero validation errors and nonempty frames at both extents |
| Does history contribute to edge coverage and reset at discontinuities? | Compare a 640x420 static frame before history and after one complete jitter cycle. Translate and rotate a tracked object, replace its identity, then cut the camera and resize. | At least 20 changed pixels with history, identical camera-cut and explicit-reset captures, invalid history on resize and anti-aliasing toggle, zero validation errors. |
| Does a material texture preserve its role through the GPU? | Read a fixed 25-pixel patch from a plain linear factor, its sRGB texture equivalent, a tilted tangent-space normal and a reloaded neutral normal. Verify both upload slots stop copying unchanged layers. | At most 50 summed sample units per color channel against the linear reference. The normal and reload checks each need at least 500 aggregate RGB units of difference. Zero validation errors. |
| Does cooking invalidate dependencies selectively? | Build a clean manifest twice, change one scene, then a shared prefab, then corrupt the scene | Stable clean manifest, one scene rebuilt after a scene edit, three after shared prefab edit, old manifest survives rejection |
| Does UDP co-op tolerate repeatable impairment? | Two loopback sockets, seeded 50 ms delay in each direction with uniform jitter up to 20 ms per direction, 2% random loss per direction, reconnect between levels | Three authoritative completions and one mirror event, both endpoints observe all completions |
| Can persisted input reproduce a level run? | Three authored command tapes against the same executable build and cooked scene revision | Exact per-tick state hashes and a final completion per level; verifier rejects altered state |

Hardware query times omit presentation and CPU waits. Host draw duration includes pacing and host stalls. Frame rows retain serials and raw timestamps so the reported duration can be independently recomputed. Entity counts exclude the static floor for the 1,000-body dynamics load. The fixture sets separate rendering, ECS, physics and network loads.

## Full profile

The Linux x86-64 reference CPU, GPU, RAM, OS and driver have yet to be pinned. The full graphics profile also requires the required animated character scene, shadow quality, material set, and all declared postprocessing. Timing a partial graph on the M2 cannot establish the full profile. The eight-hour mixed workload, clean Linux machine, editor integration review and source-bound evidence checks remain required.
