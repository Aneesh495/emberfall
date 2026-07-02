# Methods and provenance

- The Vulkan graph follows the explicit stage and access rules in Khronos's reference and synchronization validation guide. [Vulkan registry](https://registry.khronos.org/vulkan/) and [Synchronization validation](https://www.khronos.org/blog/a-guide-to-vulkan-synchronization-validation)
- The display pass uses the filmic rational fit published by Krzysztof Narkowicz under CC0. [ACES Filmic Tone Mapping Curve](https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/)
- Erin Catto's presentation describes accumulated sequential impulses and warm starting. Emberfall's translating solver implements its own narrow phase, constraint loop and tolerances. [Sequential Impulses](https://box2d.org/files/ErinCatto_SequentialImpulses_GDC2006.pdf)
- The companion uses A* with an admissible Manhattan heuristic on four-connected cells. [A Formal Basis for the Heuristic Determination of Minimum Cost Paths](https://doi.org/10.1109/TSSC.1968.300136)
- Glenn Fiedler's snapshot interpolation discussion explains the tradeoff between complete snapshots and bandwidth. [Snapshot Interpolation](https://gafferongames.com/post/snapshot_interpolation/)
