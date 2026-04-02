# Engine contract

The world uses right-handed coordinates, meters, +Y up, and -Z as default forward. Fixed simulation ticks advance at 60 Hz. Systems receive tick commands at the world boundary. Rendering consumes extracted snapshots and interpolates continuous presentation state.

Entity handles carry slot and generation. Persistent scene IDs are independent of runtime slots. Structural mutation is deferred across queries. A save belongs to a scene revision and stores only declared simulation state. The host owns authoritative network state. Same-build exact replay and cross-platform network quantization are separate contracts.

The reference frontend is SDL3 and Vulkan. The GPU feature contract must be queried at runtime, and the portability path must expose unsupported features explicitly.
