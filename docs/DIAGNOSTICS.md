# Verified portability diagnostic

## November 7 preview

Run `m2-preview-wrap-v2` used clean checkout `bd461d7bb2af9d7b402f70be7058990589004942` on Apple M2 with MoltenVK. Its evidence manifest SHA-256 is `47fd1816d2cec6ac4dc40b99addf051e8c5ffb7584b6a1a18588e5b7699d89e4`.

All 24 diagnostic commands and 18 tests passed. The standalone game replayed all three levels at 357, 372 and 453 ticks. The verifier reproduced their exact state hashes against matching cooked assets and recomputed the timing results. It rejected all 11 tamper controls for the expected reasons. The standalone client applied 49 authoritative snapshots on loopback. The separate fault-injected UDP fixture completed all three levels with three exit events and one mirror event.

The Vulkan workshop presented 40 frames with zero validation errors and 198 animated triangles from the imported Meridian heliograph. The packaged Meridian replay submitted the same asset plus the two eight-triangle player scarves. The synthetic 1080p stress workload measured 16.500 ms GPU median and 21.513 ms p99. The one-worker and four-worker ECS workloads measured 0.312 ms and 0.236 ms p95. The translating-body workload measured 3.730 ms p95.

All 18 core tests also passed under ASan and UBSan on the M2. Hosted Linux x86-64 passed the CPU evidence checks, software Vulkan fixtures, standalone replays and packaged loopback co-op on the executable commit. [Hosted diagnostic](https://github.com/Aneesh495/emberfall/actions/runs/42387772819)

`full-preview-wrap-v2` returned blocked. The release command rejected the verified preview bundle because it requires a full campaign. The [gate matrix](BUILD_STATUS.md) tracks the remaining requirements.

## November 4 continuation

Run `m2-continuation-v1` executed source commit `4d1550212d50d3687bf00fcd95ff86d9a37b8b2d` from a clean isolated checkout. The native renderer used an Apple M2 and MoltenVK 1.4.2. The evidence manifest SHA-256 is `f326b9f4b25b281fceb0b59ca9d015d30d3539f2f47e9d02e76a882b05b9dc00`.

| Workload | Observed result |
| --- | --- |
| Render stress | 160 presented frames at 1920x1080 and zero Vulkan validation errors. The 140 retained timing rows measured 13.733 ms p50 and 15.556 ms p99 GPU time. Host draw time was 16.611 ms p50 and 17.892 ms p99. |
| GPU stages | Independent p50 values were 4.412 ms for shadow and prepass, 9.077 ms for opaque, 0.193 ms for temporal resolve, 0.069 ms for effects and 0.191 ms for display. Stage percentiles use their own distributions. |
| Rendering fixtures | The blocked shadow patch darkened by 234 summed RGB units. The open patch changed by zero. Temporal camera-cut and reset frames matched byte for byte. The sRGB texture matched its linear material reference. The hidden particle burst changed zero pixels. |
| ECS and physics | One million operations matched the ECS oracle. The useful 100,000-entity workload took 0.268 ms p95 with one worker and 0.111 ms with four. The 1,000 translating-body workload measured 0.431 ms p95. Convex contact and angular response use separate analytical fixtures. |
| Packaged game | All three native solo replays completed with the expected 357, 372 and 453 ticks and exact same-build replay hashes. Each replay submitted GPU particles and a ground decal. |
| Packaged co-op | Two native processes on loopback exchanged authoritative state. The client applied 48 snapshots and the host acknowledged remote input through sequence 148. The separate impaired UDP fixture completed all three levels and emitted one mirror event and three exit events. |
| Workshop and content | Validated captures exercised the frame, collision and asset inspectors. Both native applications rendered the 64-triangle glTF monitor with three texture layers. Tests exercised transitive prefab dependencies and restored serialized undo and redo. |
| Verification | 18 tests passed in the native suite and in a separate ASan/UBSan core build. Eleven evidence tamper controls were rejected for their expected reasons. |

The stress scene submits 10,500 opaque instances and two animated eight-triangle cloth batches for 2,016,016 opaque triangles. It uses 128 point lights, three directional cascades, one six-face local shadow, temporal resolve and an additive effects layer containing 24 particles and one ground ring. The benchmark uses synthetic geometry and exercises a subset of the required final features. Prior render CSVs preserve earlier pass sets.

Linux x86-64 hosted CI passed on the frozen executable source. It ran the CPU diagnostic, software Vulkan fixtures, all three standalone replays and the two-process packaged co-op check. [Hosted run](https://github.com/Aneesh495/emberfall/actions/runs/41909147066)

Full attempt `full-continuation-attempt-v1` recorded the unavailable Linux GPU reference and outstanding render, soak and clean-machine release requirements. Both the full acceptance invocation and the release gate exited nonzero. See the [gate matrix](BUILD_STATUS.md) and [reproduction commands](REPRODUCIBILITY.md).
