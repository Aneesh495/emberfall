# glTF mesh and skeletal animation import

Declare a `.gltf` or `.glb` source in `assets/levels/ember.catalog` with a stable asset GUID.

```text
mesh prism-monitor.gltf aa4d4f1f6e7d4d0a9724f1863e882ca1
```

A local `.bin` buffer needs its own `buffer` declaration and a `mesh-buffer SOURCE BUFFER` relationship. A local `.png` image needs an `image` declaration and a `mesh-image SOURCE IMAGE` relationship. GLB BIN chunks, PNG buffer views, and base64 buffer or image data travel inside the source file. The cooker resolves names within the catalog directory. It rejects remote and parent-relative URIs.

Use the Mesh field on a decorative editor node, or run the editor's `--set-mesh SCENE ID MESH_NAME` command. The cooked mesh name uses the source stem with a `.mesh` extension. Run `make cook` after editing a source. Press F8 to reload a cooked mesh in the editor.

The supported glTF 2.0 subset contains one selected scene, triangle lists, float positions and normals, unsigned byte, short or integer indices, and opaque metallic/roughness materials. Static nodes support affine transforms. The cooker generates face normals for static primitives without authored normals. It reverses triangle order after a reflection and transforms authored normals by the inverse transpose. Morph targets, material extensions and alpha modes other than OPAQUE are outside this import path.

Base color, metallic/roughness and normal maps support 64x64 noninterlaced RGB or RGBA8 PNG. Textures use float `TEXCOORD_0`, repeating wrap and trilinear filtering. A normal map needs an authored normal and a perpendicular unit tangent with a signed handedness. The importer adjusts tangent handedness for reflected nodes. Roughness uses green and metalness uses blue. Base color uses sRGB. The cooker rejects unknown texture roles and unsupported samplers.

The cooker caps documents at 2 MB, buffer data at 8 MB per mesh, and each imported mesh at 16,384 expanded vertices and 32 primitives. The native runtime validates the cooked format separately before accepting it. It retains immutable assets across scene instances. Each scene can submit up to 32,768 imported vertices and 112 imported primitives through the existing timeline-protected Vulkan upload slots. Material factors and textures drive direct GGX lighting. Ambient reflection is a sky gradient.

`EMBMSH01` remains readable. Textured meshes use `EMBMSH02` with up to 16 baked seven-level RGBA8 mip chains, 1-based material layer indices, normal scale, UV and tangent records. The PNG decoder bounds decompression to the exact filtered image size and checks chunk CRCs. Base color mip generation averages in linear light and re-encodes as sRGB. Data mips average linear channels. The renderer re-normalizes sampled normals. [PNG specification](https://www.w3.org/TR/png-3/)

Each Vulkan frame slot owns a 16-layer texture array with sRGB and linear views. The upload compares the incoming mip bytes with that slot's retired staging data. A changed layer copies all seven mips. The graph orders sampled reads and transfer writes, and returns the array to shader-read layout before drawing. Empty scenes still initialize the descriptor image layout. The aggregate per-scene limit is 16 distinct layers. [CPU validation](../src/mesh.cpp), [frame upload](../src/gpu/frame.cpp)

## Skinned meshes

A skinned asset uses one untransformed mesh root and one skin. Joint parents must be in the skin, with up to 128 joints, 16 hierarchy levels and eight clips. Supply authored normals, `JOINTS_0` in unsigned byte or short format, and `WEIGHTS_0` as floats or normalized unsigned bytes or shorts. The cooker validates four weights per vertex and remaps the source joint indices to parent-first order. Inverse binds are rigid affine matrices. An omitted inverse-bind accessor defaults to identity, as in glTF. Local joint scales are limited to [0.75, 1.25].

`EMBMSH03` retains material fields and texture layers from v2 and adds joint records, independent TRS channels and per-vertex influences. Translation, rotation and scale channels use LINEAR or STEP interpolation. Each clip keeps its own key times, with up to 16,384 keys across an asset. The editor previews the first clip on its window clock and play mode uses simulation time. The game loops the first clip using simulation time and keeps all motion visual. There is no root-motion transfer to the physics controller.

The runtime validates and evaluates joint matrices on the CPU, skins position, normal and tangent, and uploads the result into the retired frame slot. Opaque and shadow draws use the same deformed vertices. The renderer uses current-frame samples for skinned surfaces because it lacks previous deformed vertex positions. The original Meridian heliograph uses a fixed frame, a rotating joint and three opaque materials. [Animation evaluator](../src/animation.cpp), [Cooker](../scripts/gltf_import.py), [Format checks](../tests/gltf_import.py)

The Prism Gallery places an original 64-triangle glTF monitor in the game and editor. Both programs reported 64 imported triangles and three texture layers with zero Vulkan validation errors on Apple M2. A separate readback fixture compares sRGB samples against equivalent linear material factors and tests replacing an uploaded normal layer. [Material fixture](../tests/gpu_material.cpp)

The format and coordinate requirements follow the [glTF 2.0 specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html).
