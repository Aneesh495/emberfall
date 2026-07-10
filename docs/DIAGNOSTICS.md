# Verified portability diagnostic

Run `m2-portability-v3` executed commit `70043795798c26655e640024025a96cf54b93360` from a clean isolated checkout. The renderer ran on the Apple M2 using MoltenVK 1.4.2. The evidence manifest SHA-256 is `290dee33f16717a1fa8fa933e717a6f849b0727927f72e83c900bbd590b8c84c`.

| Workload | Observed result |
| --- | --- |
| Native renderer | 14.35 ms p50 and 14.95 ms p99 GPU time. Host draw time was 16.39 ms p50 and 17.41 ms p99. |
| Directional shadow, opaque lighting, display | Pass medians were 0.73 ms, 13.39 ms and 0.26 ms. Percentiles are calculated per pass. |
| ECS | 0.277 ms p95 with one worker and 0.124 ms with four. Both 100,000-entity runs produced identical final state hashes. |
| Physics | 0.396 ms p95 for 1,000 translating bodies plus the static floor. The measured interval contained 987 to 1,000 contacts per tick. |
| Packaged game | The Aperture, Prism Gallery and Meridian replayed 357, 372 and 453 ticks. Their final hashes matched independently replayed command tapes. |
| UDP fault test | Two loopback endpoints finished all three levels under the declared synthetic impairment. The host emitted exactly one mirror event and three exit events. |
| Native UI and editor | The game exercised keyboard menus, settings persistence, focus loss, pause, restart and resize. The editor presented 25 validated frames. |
| Independent verifier | All seven rejection probes passed, including false timing, a missing pass, altered replay state, and a duplicated network completion. |

The renderer submitted 10,500 instances and 2,016,000 opaque triangles at 1920x1080 with 128 local lights. It used one 1536-square directional shadow map, HDR lighting and the display filter. There were zero Vulkan validation errors in the stress and resize tests. The benchmark geometry is synthetic and its pass set is narrower than the full profile. The opaque stage dominated GPU time in this workload.

Ubuntu x86-64 hosted CI compiled the same source, ran the CPU suite and verifier controls, exercised the software Vulkan resize and UI paths, and replayed the Meridian from the Linux package. This is a software Vulkan diagnostic. [Hosted run](https://github.com/Aneesh495/emberfall/actions/runs/41870328252)

Full attempt `full-attempt-v3` recorded the unpinned Linux hardware profile and outstanding rendering, release and soak prerequisites. The full acceptance command exited nonzero. See the [gate matrix](BUILD_STATUS.md).
