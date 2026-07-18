# Verified portability diagnostic

Run `m2-portability-v4` executed commit `dba0fe7ea2429ae4aeec311efa866fef98d895e7` from a clean isolated checkout. The renderer ran on the Apple M2 using MoltenVK 1.4.2. The evidence manifest SHA-256 is `3c609d023c2f254d7eedc4e61971af26b417f5c87eb70e91e80e9d759567f109`.

| Workload | Observed result |
| --- | --- |
| Native renderer | 14.76 ms p50 and 15.93 ms p99 GPU time. Host draw time was 16.54 ms p50 and 17.84 ms p99. |
| Directional shadow, opaque lighting, display | Pass medians were 0.75 ms, 13.76 ms and 0.20 ms. Percentiles are calculated per pass. |
| ECS | 4.347 ms p95 with one worker and 0.304 ms with four. Both 100,000-entity runs produced identical final state hashes. |
| Physics | 0.399 ms p95 for 1,000 translating bodies plus the static floor. The measured interval contained 987 to 1,000 contacts per tick. |
| Packaged game | The Aperture, Prism Gallery and Meridian replayed 357, 372 and 453 ticks. Their final hashes matched independently replayed command tapes. |
| UDP fault test | Two loopback endpoints finished all three levels under the declared synthetic impairment. The host emitted exactly one mirror event and three exit events. |
| Native UI and editor | The game exercised keyboard menus, settings persistence, focus loss, pause, restart and resize. The editor presented 25 validated frames. |
| Independent verifier | All seven rejection probes passed, including false timing, a missing pass, altered replay state, and a duplicated network completion. |

The renderer submitted 10,500 instances and 2,016,000 opaque triangles at 1920x1080 with 128 local lights. It used one 1536-square directional shadow map, HDR lighting and the display filter. There were zero Vulkan validation errors in the stress and resize tests. The benchmark geometry is synthetic and its pass set is narrower than the full profile. The opaque stage dominated GPU time in this workload. The previous verified diagnostic measured 0.277 ms p95 for the same single-worker ECS workload. The two observations show substantial timing variance on the local host.

Ubuntu x86-64 hosted CI compiled the same source, ran the CPU suite and verifier controls, exercised the software Vulkan resize and UI paths, and replayed the Meridian from the Linux package. This is a software Vulkan diagnostic. [Hosted run](https://github.com/Aneesh495/emberfall/actions/runs/41870922561)

Full attempt `full-attempt-v4` recorded the unpinned Linux hardware profile and outstanding rendering, release and soak prerequisites. The full acceptance command exited nonzero. See the [gate matrix](BUILD_STATUS.md).
