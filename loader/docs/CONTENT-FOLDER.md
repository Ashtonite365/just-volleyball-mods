# One content folder (feat/one-content-folder, 26 Nov 2026)

`mods\models` (player models) and `mods\resources` (resource packs) are now ONE folder:
`mods\resources\packs`. Every pack - outfit, hat, ball, court, **model** - is a folder there with a
`skin.json`; a pack is a player model when `"kind": "model"`.

| what | before | now |
|---|---|---|
| model packs | `mods\models\<folder>\` | `mods\resources\packs\<folder>\` (`"kind": "model"`) |
| model pick | `mods\models\equipped.json` `{"model": "BEA0"}` | `"model"` key of `mods\resources\packs\equipped.json`, next to `"equipped"` (skins) |
| model settings | `mods\models\model_settings.json` | `mods\resources\packs\model_settings.json` |
| skins, picker | `resources\packs\equipped.json`, `picker.json` | unchanged |
| download cache | `resources\cache` | unchanged |

## Loader (jvm_loader, `src/models/content_paths.*`)
* **Model scan:** kind "model" packs in `resources/packs` first, then the old `mods/models`
  (every pack there, as before; folder keys `models/<folder>` when both exist), then the cache.
  Same id in both: the `resources/packs` copy wins (registry first-wins).
* **Texture scan:** skips kind "model" packs (they are not skins, never synced as skins) - same
  rule the download cache already had.
* **Pick:** read from `resources/packs/equipped.json` when it has a `"model"` key, else the old
  `models/equipped.json`. Always **written** to `resources/packs/equipped.json` (read-modify-write:
  `"equipped"`, picker keys and anything else are kept); vanilla is written as `"model": ""` so the
  old file can't win again.
* **Settings:** read `resources/packs/model_settings.json`, else the old one; written to the new
  one. Anchor overrides (accessory_anchor) read the same file.
* **Runtime migration:** the first save moves the pick / settings into the merged folder; the old
  folder keeps working until it's removed. Status: `models.content_folder`,
  `models.legacy_models_folder`.
* Mods panel (models row) lists the merged files, plus the old ones only if they still exist.
* Content sharing: model packs are still offered from wherever they were loaded
  (`installed_dirs` = each pack's real folder); downloads still go to `resources/cache`.

## Install migration (jv_install_one_folder_*.ps1)
Game closed check -> live hashes = expected -> backup whole `mods` + `override.cfg` + `character.json`,
hash-verified -> copy `models\<folder>` to `resources\packs\<folder>` (renamed `<folder>_model` on a
name clash), hash-verified -> merge `models\equipped.json` into `resources\packs\equipped.json`
(adds `"model"` if absent; a models `"equipped"` list is kept as `"legacy_models_equipped"`, other
absent keys copied; existing keys never overwritten) -> `model_settings.json` moved if absent
(else kept, old one stays in the backup) -> `models\README.txt` -> `resources\packs\README-models.txt`
-> `mods\models` moved into the backup folder (not deleted) -> new `libjvm_loader` DLL -> verify,
`character.json` untouched, log.

## Tests
* `JV_TEST_MODE=models` - old layout (fallback path), unchanged suite.
* `JV_TEST_MODE=models-merged` - same suite on the merged layout + merged checks (model packs not
  texture packs, model id never sent as a skin, merged/old precedence, old pick fallback).
* `content-share-merged` net pair - cosmetic sharing with the host's models in the merged folder.
