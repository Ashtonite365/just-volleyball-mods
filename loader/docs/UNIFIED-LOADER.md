# Unified map loader (jvm_loader)

The texture loader and the model loader are one mod (`jvm_loader`), and that loader runs a map
pack's `goofy` section itself (no separate standalone hook).

## 1. Module structure

One library `jvm_loader` (`mods/bin/jvm_loader.gdextension`, entry `jvm_loader_init`), one bootstrap
entry, one node to manage: `/root/JVLoader` (class `JVLoader`). It has four subsystems:

| subsystem | code | node | what |
|---|---|---|---|
| texture | `src/textures/` (unchanged files) + `src/loader/native_audio*` | `/root/JVTextureLoader` | resource packs, global swaps, per-player skins + ID sync, outfits/hats/back, net/court/ball specs, stage + ball picker packs, `picker.json`, `equipped.json`, lazy native-audio API (`audio{}` / `audio.*` caps) |
| model | `src/models/` (unchanged files) | `/root/JVModelLoader` (+ `JVModelTab`) | full player models (kind "model" packs in `mods/resources/packs`, old `mods/models` fallback - docs/CONTENT-FOLDER.md), locker model tab, `model_settings.json` |
| stage / map | `src/loader/unified_loader.cpp` | (inside JVLoader) | which loader map is in play now: a `type:"stage"` pack is picked, the cosmetic tier is open and the current scene has a `Court` |
| goofy bridge | `src/loader/unified_loader.cpp` + `src/loader/bridge_logic.hpp` | (inside JVLoader) | hands the in-play map's optional `goofy` section to Goofy API 1.x, removes it on leave |

Why the texture/model code stays in `src/textures` and `src/models`: the parallel branches
(texture-persist, perf, court-sky, accessories-split) edit those files; keeping the paths means they
merge without rename conflicts. Only `register_types.cpp` of each folder is gone; `src/loader/register_types.cpp`
registers `JVTextureLoader`, `JVModelLoader`, `JVModelTab`, `JVLoader` once.

`JVLoader` spawns the two subsystem nodes (deferred) directly under `/root` with their historical names,
so `get_node("/root/JVTextureLoader")` / `("/root/JVModelLoader")` callers (picker UI, mods panel,
status.json, every probe and test) work unchanged. If a node with that name already exists (an old
two-library install still loaded it) the loader uses it and does not make a second copy. Removing
`JVLoader` frees both subsystems (each reverts its own changes in `_exit_tree`).

JVLoader API (bound, callable from GDScript / other mods):
`get_status()`, `get_bridge_status()`, `is_loader_map(id)`, `get_active_map()`,
`get_map_goofy_block(id)`, `refresh()`, `reload_config()`.
`is_loader_map(id)` is what the map-select Ball Normal/Fixed toggle (feat/goofy-ball) should use to
decide whether to show itself: true only for `type:"stage"` packs loaded by this loader.

Status: `status.json` `libraries` now has `loader` (JVLoader) plus `textures` and `models` (the
subsystems, same keys as before). Mods panel keeps the two config pages, titled
"map loader: resource packs + courts" and "map loader: player models".

Native audio (API 1.4) lives in `src/loader/native_audio*` and is reached through
`JVTextureLoader` / CORE (`audio.query`, `audio.play`). It enumerates the game's
own `res://game/sfx` and `res://game/ost` streams as a path index and only
loads a stream or creates `/root/JVM_NativeAudio` when something plays. See
docs/PICKERS.md "Pack audio" and docs/CORE-API.md "Native audio (1.4)".

## 2. Goofy section: load order / flow, step by step

Pack (`mods/resources/packs/<folder>/skin.json`), optional block:
```json
{ "id": "1022", "type": "stage", "base_stage": "grass", "court": { ... },
  "goofy": { "version": 1, "ball.gravity": { "mul": 0.4 } } }
```
Keys are Goofy param ids (feat/goofy-ball `docs/GOOFY-BALL.md`); legacy `gravity_scale`,
`bounce_scale`, `big_ball` are accepted by Goofy as aliases. The loader does not interpret the keys.

1. Boot: `bootstrap.gd` loads `jvm_core` (gate, version pin), then `jvm_loader`, `jvm_shots`, `jvm_goofy`.
   `JVLoader._ready` defers `spawn_subsystems()` -> `/root/JVTextureLoader`, `/root/JVModelLoader`.
2. Texture subsystem scans packs (process-lifetime cache from texture-persist), reads `picker.json`.
   A `type:"stage"` pack is a picker tile; the `goofy` key is ignored by the texture code.
3. Every 0.25 s `JVLoader._process` runs **update_stage()**: map in play = cosmetic tier open AND
   `get_picked_stage()` is a loader stage pack AND the current scene has a `Court`
   (`Court`, `Stage/Court`, or a descendant named `Court`). Otherwise `active_map = ""`.
4. Then **update_bridge()**:
   1. Find Goofy by name: `/root/JVGoofyMods` with methods `api_version`, `set_loader_map`,
      `apply_pack_params`, `clear_pack_params`, and `api_version()` major == 1.
      Not found / no API / other major -> stop here: the section is ignored silently, the map loads
      normally (bridge `why` says so; nothing is logged per tick).
   2. Read the in-play pack's `goofy` block from its `skin.json` (cached by file mtime; a non-object
      `goofy` is warned once and ignored).
   3. `bridge_logic::decide()` (pure, unit-tested) says which calls are due vs. what Goofy was already told:
      - map changed away from a pack whose block was applied -> `clear_pack_params(old)`
      - map changed (including to `""` = stock map) -> `set_loader_map(map)` (drives Ball FIXED in Goofy)
      - map has a block and it is not applied yet (or skin.json changed) ->
        `apply_pack_params(map, block minus "version", version)`
      - otherwise nothing (no per-tick re-apply).
   4. The result `{ok, applied, ignored, errors, active}` is logged once and kept in
      `get_bridge_status().last_result`. Errors never block the map and are not retried until the
      map or file changes.
5. Goofy enabled vs disabled is Goofy's call (its contract): it keeps the pack layer and pushes values
   into the game only while it is enabled / physics allowed (offline, Free Play). In a public lobby
   (full tier closed) the block has no effect and the stock values are back; it goes live again by
   itself when Goofy is enabled again on the same map. `get_bridge_status().goofy_enabled` mirrors
   `is_enabled()` for UI hints.
6. Leave (court gone / scene change / stage unpicked / cosmetic tier closed / loader removed):
   `clear_pack_params(id)` + `set_loader_map("")` -> stock values.
7. Goofy appears or is replaced later: the bridge notices the new instance and re-tells it the map
   and block. Goofy freed: the bridge forgets what it told it.

Precedence (Goofy's, feat/goofy-ball): stock < user `mods/goofy/*.json` < map pack block (priority 200)
< other mods' overrides (500) < Ball FIXED on a loader map (everything stock). The loader never
writes the user's Goofy files.

## 3. Compatibility notes

- Unchanged and still loading: every pack folder and ID under `mods/resources/packs/` (and the old
  `mods/resource packs/` fallback), `picker.json`, `equipped.json`, `resources/packs/<model>/` (was `mods/models/<model>/`),
  `models/equipped.json`, `model_settings.json`, `texture_sync.json`, shots config, goofy config.
  No file is moved or rewritten by the merge; there is no migration step because nothing changed
  location. Node paths, class names, bound methods and status keys are the same.
- Packs without a `goofy` key behave exactly as before. A `goofy` key in a non-stage pack is ignored.
- Bootstrap: `jvm_loader` replaces `jvm_textures` + `jvm_models`. The old two are **never loaded next
  to** `jvm_loader` (same class names would collide). If `jvm_loader.gdextension` is missing
  (half-updated install) the bootstrap falls back to the old two. Leftover old files are inert and logged:
  `obsolete jvm_textures.gdextension present (not loaded; replaced by jvm_loader) - safe to remove`.
- Version-pin mismatch: `loader`, `textures` and `models` are all reported `disabled-version-mismatch`.
- Load order changed slightly: the texture/model nodes are now added one frame after jvm_goofy's node
  (they were before it). Neither depends on Goofy; tests cover menu, locker, free play, net pairs.

### Obsolete files after installing this build (game folder `mods\bin\`)
- `jvm_textures.gdextension`, `libjvm_textures.windows.template_release.x86_64.dll`
- `jvm_models.gdextension`, `libjvm_models.windows.template_release.x86_64.dll`

New: `jvm_loader.gdextension`, `libjvm_loader.windows.template_release.x86_64.dll`, updated `bootstrap.gd`.

## 4. Tests

The tests folder is kept in the local working copy and is not published.

```
g++ -std=c++17 -Isrc tests/test_loader.cpp -o /tmp/tl && /tmp/tl            # bridge decisions (unit)
JV_TEST_MODE=loader          tests/run_integration.sh <godot>   # Goofy API 1.0 stand-in: applied / disabled / leave
JV_TEST_MODE=loader-nogoofy  tests/run_integration.sh <godot>   # Goofy not installed: ignored, map loads
JV_TEST_MODE=loader-oldgoofy tests/run_integration.sh <godot>   # jvm_goofy without the API: ignored
JV_TEST_MODE=loader-realgoofy tests/run_integration.sh <godot>  # against the real Goofy library
tests/run_all_linux.sh <godot>                                  # everything incl. the loader modes
```
