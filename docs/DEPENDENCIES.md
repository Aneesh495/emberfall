# Dependency policy

SDL3 supplies platform services. Vulkan supplies graphics commands. Dear ImGui may supply editor widgets. Emberfall implements its own world, simulation, render graph, GPU resource policy, asset model, command history, protocol, and game systems. Each integrated source archive is pinned by digest. Third-party notices accompany redistributed binaries.

## Primary references

- [SDL3 API](https://wiki.libsdl.org/SDL3/FrontPage)
- [Vulkan registry](https://registry.khronos.org/vulkan/)
- [Khronos synchronization validation guide](https://www.khronos.org/blog/a-guide-to-vulkan-synchronization-validation)
- [Khronos glTF 2.0 specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html)

The frontend pins SDL 3.4.18 (zlib) by archive digest. SDL is built statically with its GPU abstraction disabled. GLSL is built by glslang and run through the system Vulkan loader. The local portability check used glslang 16.6.0, Vulkan loader and validation layer 1.4.363, and MoltenVK 1.4.2.
