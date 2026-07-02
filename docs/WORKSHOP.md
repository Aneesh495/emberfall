# Scene workshop

Run `make editor SCENE=assets/levels/01-aperture.scene`. Use the left panel or click geometry to select a node. Shift-click extends the selection. Drag a selected object to translate it on X, or hold Y or Z while beginning the drag. G toggles 0.25 m snapping. WASD pans and the wheel zooms the viewport.

Choose an inspector row and press Enter or double-click to enter a replacement value. Press N to add a block. For a door or mirror, set kind and link ID in one edit, for example `door 10`. Enter size and color through the numeric fields. Parent IDs are accepted for decorative nodes. The validator rejects cycles, unsupported bounds, and references to incompatible devices.

Ctrl or Command with S saves the scene. Ctrl or Command with Z undoes a transaction, and Shift adds redo. F5 starts or stops play in a separate world. F6 pauses the play world, and F7 runs a single tick when paused.

Ctrl or Command with P saves the selection as a local prefab. Include every referenced parent and circuit source in the selection. V cycles the prefab browser, and I instantiates the current prefab with remapped IDs. A catalog placement can bind a prefab to a stable scene ID range so future cooking tracks the source dependency.

```sh
make cook
make package-game
```

A dirty scene is recoverable through its adjacent `.scene.recover` file. A matching base fingerprint is required to restore it. Keep both files when inspecting a recovery conflict.
