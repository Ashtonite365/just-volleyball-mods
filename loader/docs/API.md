# Loader API 1.0 — contract for the unified map loader

The unified loader (`jvm_loader`) is the texture, model, stage-pack, and native-audio library. It
registers with CORE API 1.3 and is loaded by the core bootstrap when
`mods/bin/jvm_loader.gdextension` is present.

This file is the library-side contract. Cosmetic calls still go through CORE (see the CORE API
reference in [just-volleyball-core](https://github.com/Ashtonite365/just-volleyball-core/blob/main/docs/API.md)).
Pack files are published in [just-volleyball-assets](https://github.com/Ashtonite365/just-volleyball-assets).

## Where it lives

* Library file: `mods/bin/jvm_loader.gdextension`, entry symbol `jvm_loader_init`.
* Node `/root/JVLoader` (class `JVLoader`, version `1.0`).
* Subsystem nodes `/root/JVTextureLoader` and `/root/JVModelLoader` (plus `JVModelTab`), spawned by
  `JVLoader` under those historical names so existing callers keep working.
* Soft dependency: if the loader is missing, core still runs; packs and custom models are not applied.
  Goofy is also optional: a stage pack's `goofy` block is ignored when Goofy is not installed.

## Bound methods on JVLoader

`get_status()`, `get_bridge_status()`, `is_loader_map(id)`, `get_active_map()`,
`get_map_goofy_block(id)`, `refresh()`, `reload_config()`.

`is_loader_map(id)` is true only for `type:"stage"` packs this loader has loaded.

## Status

`status.json` `libraries` has `loader` (JVLoader) plus `textures` and `models` for the subsystems.

## Further documentation

* [UNIFIED-LOADER.md](UNIFIED-LOADER.md) — module structure, Goofy bridge, load order
* [SKINS.md](SKINS.md) — pack layout and skin behaviour
* [TEXTURE_SYNC.md](TEXTURE_SYNC.md) — pack IDs and sync
* [MODELS.md](MODELS.md) — custom player models
* [CONTENT-FOLDER.md](CONTENT-FOLDER.md) — the one packs folder
* [CONTENT-SECURITY.md](CONTENT-SECURITY.md) — pack files are data only
* [CONTENT-SHARING.md](CONTENT-SHARING.md) — cosmetic sharing and auto-download
* Picker UI: [core docs/PICKERS.md](https://github.com/Ashtonite365/just-volleyball-core/blob/main/docs/PICKERS.md)
