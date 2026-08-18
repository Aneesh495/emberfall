# Engine contract

The world uses right-handed coordinates, meters, +Y up, and -Z as default forward. Fixed simulation ticks advance at 60 Hz. Systems receive tick commands at the world boundary. Rendering consumes extracted snapshots and interpolates continuous presentation state.

Entity handles carry slot and generation. Persistent scene IDs are independent of runtime slots. Structural mutation is deferred across queries. A save belongs to a scene revision and stores only declared simulation state. The host owns authoritative network state. Same-build exact replay and cross-platform network quantization are separate contracts.

The reference frontend is SDL3 and Vulkan. The GPU feature contract must be queried at runtime, and the portability path must expose unsupported features explicitly.

Prefab bases use the canonical scene format with at most 64 nodes. A variant begins with `ember-prefab 2`, followed by a quoted local `base` filename and up to 256 unique `set LOCAL_ID FIELD "VALUE"` records. The resolver follows at most eight same-directory sources, rejects cycles and symlinks, and validates the scene after applying each layer. Fields are name, position, size, yaw, color, parameter, destination and mesh. Overrides preserve structure and persistent local IDs. Instantiation remaps parent and circuit references and applies the placement offset to root transforms and platform destinations. The cooked scene manifest records all ancestor source hashes. [Resolver](../src/prefab.cpp), [instantiation](../src/editor.cpp), [dependency discovery](../scripts/cook.py)
