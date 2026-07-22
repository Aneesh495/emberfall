# glTF static mesh import

Declare a `.gltf` or `.glb` source in `assets/levels/ember.catalog` with a stable asset GUID.

```text
mesh prism-monitor.gltf aa4d4f1f6e7d4d0a9724f1863e882ca1
```

A local `.bin` buffer needs its own `buffer` declaration and a `mesh-buffer SOURCE BUFFER` relationship. GLB BIN chunks and base64 buffer data travel inside the source file. The cooker resolves names within the catalog directory. It rejects remote and parent-relative URIs.

Use the Mesh field on a decorative editor node, or run the editor's `--set-mesh SCENE ID MESH_NAME` command. The cooked mesh name uses the source stem with a `.mesh` extension. Run `make cook` after editing a source. Press F8 to reload a cooked mesh in the editor.

The supported glTF 2.0 subset contains one selected scene, static triangle lists, float positions and normals, unsigned byte, short or integer indices, affine node transforms, and opaque linear base color, metallic, roughness and emissive factors. The cooker generates face normals for primitives without authored normals. It reverses triangle order after a reflection and transforms authored normals by the inverse transpose. Skinning, clips, morph targets, texture bindings, material extensions, and alpha modes other than OPAQUE are outside this import path.

The cooker caps documents at 2 MB, buffer data at 8 MB per mesh, and each imported mesh at 16,384 expanded vertices and 32 primitives. The native runtime validates the cooked format separately before accepting it. It retains immutable assets across scene instances. Each scene can submit up to 32,768 imported vertices and 112 imported primitives through the existing timeline-protected Vulkan upload slots. Material factors drive direct GGX lighting and emissive output. Ambient reflection is a sky gradient.

The Prism Gallery places an original 64-triangle glTF monitor in the game and editor. Both programs reported 64 imported triangles with zero Vulkan validation errors on Apple M2.

The format and coordinate requirements follow the [glTF 2.0 specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html).
