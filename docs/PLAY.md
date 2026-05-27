# Playing The Lantern Reach

Unpack the archive and run `./play` from the extracted directory. The game stores its checkpoint and settings in the per-user SDL preferences directory. Pass `--profile DIRECTORY` to choose an isolated profile. The macOS preview bundles MoltenVK and the Vulkan loader. On Linux, install a Vulkan 1.3 GPU driver and loader before launching.

| Action | Keyboard | Gamepad |
| --- | --- | --- |
| Walk | WASD | Left stick |
| Jump | Space | A / south |
| Use lantern or push crate | E | B / east |
| Switch explorers in solo | Tab | Y / north |
| Crouch | Left Ctrl | Right shoulder |
| Pause | Escape | Start |
| Navigate menus | Arrows, Enter | D-pad, A / south |
| Fullscreen | F11 | Settings menu |

The idle explorer stays where you leave them. The Aperture needs an explorer on each of the two plates. One explorer holds the Prism interlock while the other rotates the lantern. In the Meridian, board the ferry before powering it from shore. Beacons save both explorers at a safe position on the current level.

## Local network play

Run `./play --host 48732 --key SHARED_KEY` on the host. Run `./play --join HOST_IPV4 48732 --key SHARED_KEY` on the other machine. Use the same content package on both machines. The host controls level changes and owns the save. The key identifies the session on a trusted network.
