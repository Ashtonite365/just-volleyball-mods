# jvm_models - custom 3D player models (JVModelLoader, cosmetic tier)

Swaps the look of the player rig for a model from a resource pack, at runtime only
(no `.pck`/`.exe` edits). The game's own skeleton, animations, hand reach and scaling keep
driving everything (team colours do **not** reach the model, see below); the model is purely visual and **local only**:
nothing about it is sent to other players.

## Pack layout
```
mods/resources/packs/<pack>/skin.json            {"kind": "model", ...}  (next to the texture packs)
mods/resources/packs/<pack>/<model>.glb          (self-contained binary glTF only; docs/CONTENT-SECURITY.md)
mods/resources/packs/equipped.json               {"equipped": [skins...], "model": "AE01"}
mods/resources/packs/model_settings.json         (locker FULL MODEL tab settings)
mods/models/...                                  old layout: still read as a fallback (docs/CONTENT-FOLDER.md)
```
`skin.json`:
```json
{ "kind": "model", "id": "AE01", "name": "Aero", "model": "aero_player.glb",
  "tint_param": "albedo", "tint_light": {"sss_tint": 0.42}, "color": "#FF1A8C",
  "game_hands": "keep", "hide_base": true, "apply_to": "local",
  "flash": true, "allow_scripts": false, "max_rest_error": 0.05 }
```
| key | default | meaning |
|---|---|---|
| `kind` | - | must be `"model"` (other kinds belong to the texture loader) |
| `id` | - | 4 hex digits, shares the id namespace with texture packs (duplicates are rejected for both) |
| `model` | - | pack-relative path; absolute paths, `res://`, `user://`, drive letters and `..` are refused |
| `team_tint` | `none` | **disabled**: team colour is never applied. `jersey`/`skin`/`team` are accepted but ignored with one warning per pack |
| `tint_param` | `albedo` | shader parameter (ShaderMaterial) receiving the fixed `color`; StandardMaterial3D uses `albedo_color` |
| `tint_strength` | 1 | parsed (0..1) but unused while team tint is disabled |
| `tint_light` | {} | extra shader params set to the fixed `color` lightened toward white by the given amount |
| `color` | - | fixed colour written to `tint_param` / `albedo_color` (+ `tint_light`); absent = the model's own colours |
| `game_hands` | `keep` | the game's floating hand spheres: `keep`, `hide`, `tint` (the model's own / fixed colour) |
| `arms` | - | `{"mode": "follow_hands", ...}`: the model's arms are drawn by jvm_models from a shoulder point to the game's own hand spheres every frame (see below); absent / `"model"` = the model's own (skinned) arms |
| `hide_base` | true | hide the base character meshes while the model is on |
| `apply_to` | `local` | `local` = only your own player (plus menu / locker presentation rigs); `all` = every player on your screen |
| `flash` | true | mirror the game's hit flash (`flash`/`flash_color` shader params) |
| `allow_scripts` | false | ignored (warning): packs are data only, `.tscn`/`.scn`/`.gltf` models are always refused |

**Which pack is used:** only the `"model"` key of `equipped.json` selects a model (one at a
time). Model ids in `"equipped"` are ignored with a warning - the texture loader would send
them to other players as skin ids, so keep them out of that list. No `"model"` key / empty =
vanilla. Changes apply live (`reload_config()`; the mods panel reloads it).

## How it works
1. **Gate:** follows the cosmetic tier exactly like textures (docs/GATE.md); tier OFF or
   unknown -> every graft is removed and the base visuals are restored.
2. **Hook:** installs a Callable into the game's static `ModelRig.model_hook`
   (model_rig.gd: called with each new rig's scene in `setup()`); an existing foreign hook
   is chained (called first) and restored on exit. Existing rigs are found by a tree scan.
3. **Graft after setup:** the hook only records the rig; the graft runs deferred, after
   `setup()` finished, so the game's own measurements (`hand_radius`, `hand_rest`,
   `_base_scale` from TARGET_HEIGHT) are computed from the vanilla body and stay identical.
4. **Skeleton binding:** every skinned mesh of the model is reparented under the game's
   `Armature/Skeleton3D` with a new Skin using *named* binds (`skeleton=".."`), so the
   game's AnimationPlayer drives it. Bone names resolve exactly, then case-insensitively
   with `. _ - space` equal (glTF exporters rename `hand.L` -> `hand_L`). Any missing bone ->
   fallback. Rest-pose differences above `max_rest_error` are reported as a warning.
5. **Base visuals:** every non-model GeometryInstance3D/Decal under the rig is hidden each
   frame (the game re-shows parts), and restored on ungraft. Render layers are copied from
   the base meshes (reflections / stencil passes keep working). The sweep never hides another
   rig's graft (`jvm_model` meta) or anything under another ModelRig, and the re-hide hook
   records a stock mesh's render layers before zeroing them so ungraft restores them.
   **Floor reflections:** the game's reflection copy (`player.gd` `_ensure_reflection`:
   `ModelRig.setup(true)` under the top-level `Player/_reflect_root`) is never grafted - it
   never calibrates its hands (`_bone` -1) and shares the Player's `Visual`, so grafting it hid
   the real graft (layers 0) and flooded `get_bone_global_pose(p_bone = -1)` from `hand_local`
   (PC with live reflections, 16:21). `follow_hands` arms only call the rig's `hand_local` when
   `hands_animated()` is true.
6. **Colours:** team colour is ignored (see below); the game's hit flash is still mirrored
   from the rig's materials.
7. **Resources:** the `.glb` is read as bytes, sanitised (no URIs, allow-listed extensions,
   no extras) and loaded with `GLTFDocument::append_from_buffer`; never ResourceLoader.
   `.gltf`, `.tscn` and `.scn` are refused in both layouts (docs/CONTENT-SECURITY.md).
8. **Eligibility:** the rig's nearest `Player` ancestor must be the local player
   (`texture_sync.json` `local_player_property` / `players_property`); rigs without a
   Player (menu, locker) get it; `apply_to:"all"` applies to everyone.
   **Local player only, any model folder, any id, equipped.json or the
   picker/locker, re-equips included:** a `PresentationCharacter` (title crowd, menu cards,
   locker lineup, eyecatcher, studio render; character/presentation_character.gd) also has a
   `player` property, which used to match the local-player heuristic, so the title crowd and every
   menu card wore the model. A PresentationCharacter now counts as "me" only when its
   `character` is `GameConfig.character` or has the same `look_dict()` (ignoring `"h"`, which
   the menu card overrides). The main-menu and profile pictures come from a separate
   `CharacterPortrait` player (`character_portrait.gd`): a process-disabled `Player` in a
   transparent `own_world_3d` SubViewport under the tree root, not `menu.player`. That display
   used to fail `is_eligible` (it is not the scene's `player` / `_peer_players` entry and is
   not a `PresentationCharacter`), so the 3D court character could show the model while the
   menu portrait stayed vanilla. It now counts as "me" under the same `look_dict()` rule, and
   the eligibility check is redone every frame for that rig so a portrait capture (a few
   frames after `set_character`) is not missed. Match players keep the old rule (only the
   local Player). Arms are grafted by the same code, so they follow the same rule. Texture
   packs apply to that portrait through the same look check (see SKINS.md).
9. **Fallback:** any error (bad file, missing bone, scripts, path escape, missing file,
   invalid json) -> vanilla for that pack, logged once to `jv_mods.log` and shown in
   `status.json` (`models` section: state, equipped, applied, fallbacks, last_error, warnings).

## Colours: team colour is ignored
The loader **never** applies the team / jersey / skin colour to a model. Every model keeps
its own material colours, the same on every team and in every format:
* **ShaderMaterial** (`.tscn`/`.scn` with its own shader): the shader's own `albedo` and
  `tint_light` defaults (e.g. `sss_tint`) are left untouched. The Aero bean `.tscn` stays its
  own red (`albedo` #D80836).
* **StandardMaterial3D** (`.glb`/`.gltf`): `albedo_color` stays at the file's base-colour
  factor, so a coloured base-colour texture shows as authored (the Aero bean `.glb` stays red).
* Old packs with `"team_tint": "jersey"|"skin"|"team"` still load; the value is ignored and
  one warning per pack is logged (`jv_mods.log`, `status.json` warnings).
* `"color": "#RRGGBB"` still applies: one fixed colour written to `tint_param` (ShaderMaterial)
  or `albedo_color` (StandardMaterial3D, multiplied by its texture), plus `tint_light` params
  lightened toward white.
* `game_hands: "tint"` colours the game's hand spheres with the model's own colour (shader
  `albedo`, or `albedo_color` x the base-colour texture's average for a `.glb`), or the fixed /
  locker colour.

## Locker "full model" tab
A tab in the game's own locker (runtime only, `src/models/model_tab.cpp`), added while the
models library is on (cosmetic tier):
* **Where:** one rail tile labelled `full model`, right after `outfits` and before
  `presets` / `studio`: the per-part sections (face ... outfits) come first, then the whole-body
  swap, then saving whole looks (presets) and the art studio. Built with the game's own
  `UiStyle.rail_tile` (icon `singleplayer`), registered in the locker's `_rail_btns` in the same
  order, so the locker paints it and the controller tab buttons (`cycle_section`) reach it. If the
  9th tile would not fit the rail (e.g. 1280x720) every tile gets the game's own slim rail size
  (`UiStyle.slim_rail_tile` "mid", then "short"). Not added to split-screen lockers
  (`section_mode`): models are for your own player only.
* **Page:** built in the locker's own editor panel with its own builders (`_group_header`,
  `_choice_grid`, `_swatch_grid`, `_color_field`, `_labeled`):
  * model list: `none (vanilla)` + every model pack (broken packs marked); a click **saves** it
    straight away (`equipped.json` `"model"`, other keys kept; vanilla removes `"model"`), shows it
    on the locker's own preview player and plays the locker's pick gesture;
  * `in use` when shown = saved; `save this model` appears only for a pick that could not be
    written (bad `equipped.json`) or an API preview; leaving the locker saves what it shows
    (it no longer reverts to the previous model - a vanilla pick used to snap back to BEA0);
  * **colour:** the model's own colour first (shader `albedo` default / `.glb` base-colour texture
    average, e.g. the bean `#D80836`), 10 preset swatches, a custom colour picker (live) and
    `reset to model default`;
  * **hands:** `keep` / `hide` / `model colour` (`game_hands`);
  * **shown on:** `just me` / `everyone on my screen` (`apply_to`).
* **Saved in** `mods/resources/packs/model_settings.json` (read fallback: old `mods/models/model_settings.json`) (the mod's config, never game saves):
  `{"version": 1, "models": {"AE1B": {"color": "#2F6BFF", "game_hands": "hide", "apply_to": "local"}}}`.
  Absent key = the pack's `skin.json` value. Written 0.5 s after the last change (picker drags)
  and when the locker closes.
* **How the colour applies:** like the fixed `color` field (wins over it): `tint_param` +
  `tint_light` on a ShaderMaterial; on a StandardMaterial3D `albedo_color` = the colour and a
  coloured base-colour texture is swapped for a grey copy of itself (its shading normalised to
  its average), so the result is the picked colour, not colour x texture. Reset puts the authored
  material values back. Team colour is still never used.
* The locker preview player (`locker.gd` `_player`) counts as your own player (models apply to it).

## Limitations
* The model's own AnimationPlayer/animations are ignored (the game's 26-bone clips drive it).
* Rest-pose mismatches are only warned about; a model built on a very different rest pose
  will deform.
* Restore re-shows nodes that the game itself hid while the model was on (the game
  re-applies its own visibility on the next state change).
* Selection highlight / contour outline passes of the base body don't apply to the model.
* Matches and Steam lobbies are untested on the real game (no Steam on the build box).

## Arms that follow the game's hands (`"arms"`)
The game places its hand spheres (Visual/HandL|HandR) at `hand_local()` (hand bone + a constant
offset, ~0.09 m off the bone) in idle/animated poses, and at fixed targets (`POSE_BUMP_*`,
`POSE_SET_*`, `POSE_SERVE_*`, block, windups) otherwise, so skinned arms never meet them (without this, tubes hang at the sides and hands float). With `"arms": {"mode": "follow_hands"}`
each arm is two tapered segments + joint spheres (the model's own material, so colour/flash follow)
from `shoulder` (rest point, x mirrored, moves with `anchor_bone`) through an elbow (two-segment solve,
bend toward `elbow_hint`, lengths `upper`/`lower`) to the hand sphere centre; an arm further than
`upper+lower` stretches straight. Skeleton units. Updated every frame after the game's HandSync,
from interpolated transforms; nodes are part of the graft (removed with the model). hand_radius and
base_scale are untouched. Defaults: `anchor_bone` spine1, `shoulder` [0.50, 2.42, 0], `upper` 0.45,
`lower` 0.72, `radius` [0.15, 0.16, 0.155], `elbow_hint` [1, -0.5, -0.6].
Status: `get_status().arms` = {mode, rigs, max_stretch}.
Plump Bean (BEA0) uses it; its glb is rebuilt without the skinned arm tubes by
`tools/build_bean_follow_arms.py` (same head/torso/legs as CH1's generator, imported read-only).

## Accessory anchors (fix/anchors)

Every ModelRig skeleton (the player, the locker preview, Free Play / match players, the reflection
rig) gets two `BoneAttachment3D`s created by the loader, on that skeleton's own bones:

| anchor            | bone lookup (first hit)                                                                 |
|-------------------|-----------------------------------------------------------------------------------------|
| `JVM_Anchor_head` | `anchors.head.bone` override, `head`/`Head`/`mixamorig:Head`..., any bone named `*head*`, the game's head mount bone, highest centre bone |
| `JVM_Anchor_back` | `anchors.back.bone` override, the game's `SPINE_BONES` (`spine1`...), `chest`, any `*spine*`/`*chest*`, centre bone at 60 % of head height |

Each worn accessory instance (the game's own `_acc_roots[slot]` roots - our catalog `JvmAcc_*`
items and stock hats/tails - plus our `JVM_Worn_*` / pack copies) is moved under its anchor in its
own `JVM_Seat_<slot>` node. Slot class comes from the game mount it was created on (head mount ->
head anchor, Chest/Hips Mount -> back anchor), not from its name. The seat's LOCAL transform is fixed:
`rest(anchor_bone)^-1 * C * rest(mount_bone)`; the root keeps the local transform its owner wrote, so
the game can keep re-posing it (locker sliders) and nothing ever writes a global transform or
re-snaps per frame. Seats are only recomputed when the measured body changes (model graft / swap).

`C` (skeleton space, measured from REST skinned vertex bounds, trimmed 2 %/98 %):
- our hat (`head*` slot): origin -> model head top - 0.05 m, scale 0.90
- our tail (`extra*` slot): origin -> model back point (55 % height, back face) - 0.05 m
- stock game accessories: their game placement + (model head top / back point - stock one)
- a 2nd/3rd copy of the SAME item: hats stack +0.06 m, tails fan out +/-0.07 m

Per-model override in `mods/resources/packs/model_settings.json` (old `mods/models/` copy read if that is missing):

```json
{ "models": { "BEA0": { "anchors": {
    "head": { "bone": "head", "offset": [0, -0.02, 0], "scale": 0.85 },
    "back": { "bone": "spine1", "offset": [0, -0.03, 0.01] } } } } }
```
`offset` is in meters (added to the computed seat), `scale` replaces the hat scale (head) or scales
the back accessory. The file is re-read within 2 s. Debug: `JVTextureLoader.describe_accessory_anchors(player)`.
