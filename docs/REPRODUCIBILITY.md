# Reproducibility and recovery

The portability profile is a native hardware diagnostic on the Apple M2. Install the Apple command-line tools and run the following from a clean source checkout.

```sh
brew install cmake ninja vulkan-headers vulkan-loader vulkan-tools molten-vk vulkan-validationlayers glslang
EMBERFALL_VALIDATION_LIB="$(brew --prefix vulkan-validationlayers)/lib:$(brew --prefix vulkan-loader)/lib" \
  make acceptance PROFILE=portability-diagnostic RUN_ID=m2-example
make verify EVIDENCE=evidence/m2-example
make controls EVIDENCE=evidence/m2-example
```

Use a new run ID for each invocation. The runner records command exits, exact source and content digests, environment, the cooked manifest, raw workload records, framebuffer captures, and replay tapes. Verification requires the same source commit and replay tool bytes. Negative controls create independent copies and rehash altered artifacts before invoking the verifier. Their invocation and rejection records are saved beside the evidence directory.

A CPU-only campaign uses `PROFILE=cpu-diagnostic FRONTEND=OFF`. It exercises archetype migration, scheduling, collision, serialization, cooking, editor history, and the two-endpoint UDP route. The Linux software Vulkan workflow provides a separate window and packaging diagnostic.

```sh
make acceptance PROFILE=full RUN_ID=full-attempt
```

The full invocation currently exits nonzero and writes its blocked prerequisites. It requires the declared Linux hardware reference and the complete rendering, correctness and soak workloads. The release command requires independently verified full evidence.

## Recover local state

The editor saves a same-directory journal after a committed edit. A version 2 journal stores a bounded baseline scene, before and after node records, and the current cursor. Recovery verifies the base fingerprint and journal checksum, then revalidates each transaction before restoring undo and redo. The editor can also reopen version 1 snapshot journals. If the command journal reaches 16 MiB, recovery falls back to a version 1 snapshot and preserves the edited scene. If the base changed, copy the scene and journal to a recovery directory before inspecting or reconciling either file. Saving deletes the journal after the scene has been published.

The game reads `settings.save` and `campaign.save` from its profile. Use a fresh `--profile` directory for diagnostics. A rejected progress record remains available to inspect. Starting a new expedition and saving a new checkpoint writes the current schema.
