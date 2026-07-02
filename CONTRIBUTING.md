# Contributing

Keep world, rendering, editor, and protocol ownership boundaries explicit. A new scene component needs bounds, canonical serialization, and a documented relationship to runtime state. Rendering changes should identify their image reads and writes and include a Vulkan synchronization validation run.

Run `make test FRONTEND=OFF` for core changes, `make docs` for prose and link checks, and the appropriate diagnostic profile before making measurement claims. Retain raw inputs and observations with the source commit. Use the frozen workloads when comparing results.

The [build status](docs/BUILD_STATUS.md) lists current capability gaps. The [architecture notes](docs/ARCHITECTURE.md) trace the active game and editor paths.
