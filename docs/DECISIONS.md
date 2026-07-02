# Architecture decisions

## 01. Keep runtime identity separate from scene identity

Archetype migration moves component rows and can reuse a dead entity slot. Persistent scene IDs survive serialization, prefab placement and rebinding. The slot generation prevents a reused slot from reviving a stale handle.

## 02. Use one structural barrier

Queries expose references or contiguous ranges and reject structural mutations for their duration. Queued mutations preserve iteration stability. Callers that need an atomic edit use the document transaction layer, whose validation rules are stronger than an ECS command flush.

## 03. Reject unordered access hazards before starting workers

The scheduler checks ancestry for any shared resource with a writer. A shared ready queue is simpler to bound and inspect than work-stealing deques for these small frame batches. It trades a single mutex for explicit task ownership and predictable cancellation.

## 04. Store affine world matrices

Negative and nonuniform scales can create shear in composed transforms. A world matrix preserves that result and gives the editor a well-defined local translation through the parent's inverse. Gameplay nodes are root-level so their collision dimensions remain predictable.

## 05. Keep the render graph ordered and single-queue

Image layouts and pipeline dependencies are explicit and can be validated on both desktop Vulkan and MoltenVK. Per-frame dedicated allocations postpone transient aliasing until the renderer has workloads where aliasing can be measured.

## 06. Use shader comparisons for shadow samples

The portability validation layer rejected mutable comparison samplers under the negotiated MoltenVK feature set. Shader-side comparison retains the same nine-sample depth filter on both platforms. The added fragment work is visible in the GPU pass timings.

## 07. Separate fixed-step state and visual interpolation

Player positions carry previous and current values for display. Collision and gameplay consume fixed-step state. This makes gameplay replay independent of display cadence and avoids quantizing simulation to presentation frames.

## 08. Cook native scenes through the engine parser

The recipe orchestrates hashing and invalidation, while the composer uses the same typed parser and validation as the runtime. The manifest records source and output bytes. A shared prefab edit invalidates only scenes that name it.

## 09. Lease immutable streamed versions

Readers hold shared ownership of the loaded scene. A new version is admitted only within the cache's byte budget. Failed reloads retain the previous version. Retired pinned versions remain charged until their leases expire.

## 10. Make checkpoint recovery intentionally coarse

A progress file stores room revision and checkpoint identity, and respawns both actors at the beacon with mechanisms reset. A replay captures exact tick behavior separately. The coarse save avoids combining stale device state with a new revision.

## 11. Send snapshots as replaceable state

The UDP host publishes complete quantized states. Partial or old snapshots can be discarded without invalidating the next snapshot. Input edge counters persist across repeated input packets. This costs bandwidth relative to a delta baseline and avoids long dependency chains under loss.

## 12. Keep diagnostic evidence separate from full acceptance

The verifier checks required samples against an expected source checkout and replays exact tick data. The Apple M2 pilot measures a portability graph and cannot certify the Linux hardware gate. A release requires a separately verified full campaign.
