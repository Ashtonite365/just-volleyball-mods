# Just Volleyball — original skin packs (plump bean)

Runtime-only resource packs. Published packs live in the [just-volleyball-assets](https://github.com/Ashtonite365/just-volleyball-assets) repository and install into `mods/resources/packs/<pack>/`. Nothing in the game files is changed. Geometry, rig and height of the iter_06 plump bean are not modified.

Base models (iter_06, sha256):

- `aero_bean.glb` `fd05af9493ca8d5c275318f9b476c47c48d51dc449352a42aa3fa7606834343e`
- `aero_bean.tscn` `1edfb6c23e59977ff88193b328fa4478fd8c819b9644b05651d4478e945a0653`
- `aero_player.glb` `2338ac23142c7663f69f6897b84cfd4e88f61ce107a9f08b3eaa739bbcf3bebc`
- `aero_player.tscn` `932d7f35e3cac27d3e22155d1cef7b3683344bad231e2c6e29e056c1b002410a`

Skins carry their own colours/textures. The model loader ignores team colour.
`apply_outfit_to` tints both `BaseMaterial3D` and `ShaderMaterial`, including a
`material_override`, and maps aeromesh / bean / aero mesh names to the `body`
slot so the same pack works on `.glb` and `.tscn`.

Preview sheets (front + 3/4, idle + spike) are at `renders/skins/skin_NN.png`.

## Packs

| # | id | folder | kind | what it does |
|---|----|--------|------|----------------|
| 01 | 1001 | `01_crimson_striker_outfit` | skin | Crimson Candy Outfit. `body` / `skin` / `all` tint `#C4142E` for the plump bean; jersey / shorts / shoes textures plus collar / accent / sock colours remain for the vanilla player. |
| 02 | 1002 | `02_champion_cap_hat` | skin | Champion Cap, detailed six-panel redo (design `renders/skins/skin_02_design.md`). Original `cap.glb` on `head`. Default (game character) offset `(0, 1.08, 0.03)`, scale `1.0`, tilt `-12°`. On Plump Bean (`BEA0`) a `fit` block lowers it slightly, moves it 0.05 back, tilts `-8°`, and tucks the size strap against the sphere. Toon shader. The `.glb` is not modified. |
| 03 | 1003 | `03_flame_tail_back` | skin | Flame Tail, detailed redo (design `renders/skins/skin_03_design.md`). Original `tail.glb` on `chest` (spine1). Offset `(0, -0.08, -0.45)`, scale `1.0`, tilt `-6°`. Toon shader. Jiggle / SpringBone: stiffness 0.6, drag 0.45, gravity 0.15, prefix `tail_`. |
| 04 | 1004 | `04_crimson_gold_net` | global | Championship Net. Crimson/gold `net_tape.png`, gold cord `#ffe066`, navy posts `#1c2d5a`, crimson-gold `post_pad.png`. |
| 05 | 1005 | `05_royal_court_and_autumn_grass` | stage | Royal Court & Autumn Grass. Appears as a tile in the stage picker (after Outer Space). Rust floor `#a54424` + `court_floor.png`, gold lines `#ffd633`, autumn grass tint `#e68822` + `autumn_grass.png`. Not always-on; selected via the picker (docs/PICKERS.md). |
| 06 | 1006 | `06_galaxy_ball` | ball | Galaxy nebula (brightness pass: Rec.709 mean Y 0.332, was 0.125, in line with Fire). 2:1 equirect `ball_tex.png`, `wrap: equirect`. Shown as a tile in the ball picker (docs/PICKERS.md). |
| 10 | 1010 | `10_fire_lava_ball` | ball | Fire & lava. Same equirect cube-sphere visual; listed in the ball picker. |
| 11 | 1011 | `11_beach_ball` | ball | Beach ball (8 gores + polar caps). Same path. |
| 12 | 1012 | `12_tricolor_ball` | ball | Classic tri-colour 18-panel volleyball (white / navy / gold). Same path. |

`skin.json` uses `mount` for accessories (`bone` is accepted as an alias). Default hat and back offsets are used on the game's own character. A per-model `fit` block on `hat` or `back` (keyed by model id, e.g. `BEA0`) can override `offset`, `rotation_deg`, `scale`, `mount`, and optional `parts` (per-node offset / rotation / scale, matched by node name) when that model is actually grafted on the player. Missing fields keep the accessory default. This is how the Champion Cap sits on the plump bean's sphere head without changing `cap.glb` or the cap on the vanilla character.

The Champion Cap's default mount (game character): `head`, offset `(0, 1.08, 0.03)`, tilt `-12°`, scale `1.0`. Plump Bean `fit`: offset `(0, 1.06, -0.02)`, tilt `-8°`, and the `SizeStrap` / `StrapHardware` nodes scaled by `(1, 1, 0.20)` about the strap anchors so the hanging back strap follows the nape instead of sticking out as wings. Flame Tail is unchanged.


## Locker accessories (head / face / extra)

Hat, face and back packs are no longer always-on skins bundled with whatever is
in `equipped.json`. Each one is registered as its own item in the game's locker
**accessories** section, in the existing sub-categories:

| pack | id | locker category |
|------|----|-----------------|
| Champion Cap (`02_champion_cap_hat`) | 1002 | **head** |
| a pack with a `face` block or `"category":"face"` / `"type":"face"` | (pack id) | **face** |
| Flame Tail (`03_flame_tail_back`) | 1003 | **extra** |

They can be equipped independently (the cap without the tail, and the other way
around) and independently of the full-model tab and of outfit / ball / stage
picks. Stock locker items are untouched.

`skin.json` may set `"category": "head"|"face"|"extra"`. If it is omitted, a
`hat` block maps to head, a `face` block (or `type: "face"`) to face, and a
`back` block to extra. A pack that contains more than one of those blocks is
split into separate catalog entries (`<id>_hat`, `<id>_face`, `<id>_back`).

**Migration:** if `equipped.json` already lists 1002 / 1003 (or another
accessory id), that id is copied into the first *empty* slot of its category
(`head`, `head2`, `head3`, …) on your CharacterConfig. Occupied slots are never
overwritten, so a cap you already picked in the locker is kept. Unequipping an
item in the locker removes its id from `equipped.json`; outfit ids such as 1001
stay. `picker.json` is not touched. If every slot in a category is already
full, the pack stays listed in the catalog and is applied the old way until
you free a slot.

No custom face pack is shipped in this build: the FACE category is ready, and
a pack dropped into `mods/resources/packs/` with a `face` block appears there
the next time the locker opens.

## Main-menu and profile portraits

The small character picture on the main-menu bar and the profile page is not the court
player. The game renders it with `CharacterPortrait.render` / `head_cached` into a
private SubViewport (`character_portrait.gd`). That body is dressed with
`set_character` and captured after a few frames. Skins, locker accessories and custom
models now apply to that body when its look matches `GameConfig.character` (the same
rule as a `PresentationCharacter`), using a separate apply slot so the court player's
overrides are not cleared. After the cosmetic gate turns on, and whenever packs are
re-applied, the menu reloads those pictures so a portrait captured during the 0.5 s
gate hysteresis is not left vanilla.

## Preview honesty

Sheets 01–03 are the iter_06 plump bean in a Godot harness, with outfit tint or accessories applied the same way the loader does at runtime (shader albedo / BoneAttachment3D). Sheets 04–06 are the same bean in a stand-in court built from the pack textures and authored colours — not a capture of the real Just Volleyball court scene.


## Custom balls (type `ball`)

Custom balls are **not** painted onto the game's school / pro / outdoor panel UVs. When one is
selected, a smooth cube-sphere is attached under the ball's `_model` (so it spins and squashes
with the real ball, keeps the outlines / shadow / trail / landing ring, and is picked up by the
game's hologram pass). The shader samples a **2:1 equirectangular** PNG from the object-space
direction (`V = 0` at the top of the image, the north pole; `U` wraps, with a seam on the back).
The visual is sized to the dressed model (or `PHYS.radius`), so goofy `big_ball` is not applied
twice. Selecting school, pro or outdoor (empty `picker.json` `ball`) leaves those game models
completely untouched. Galaxy is no longer an always-on global overwrite.

```
mods/resources/packs/<folder>/skin.json
{
  "id": "1006",
  "name": "Galaxy",
  "type": "ball",
  "preview": "preview.png",
  "ball": { "albedo_tex": "ball_tex.png", "wrap": "equirect" }
}
```

Select one with the ball picker, or without the picker via `mods/resources/packs/picker.json`
`{"ball":"1006"}` (or the same key in `equipped.json`) or `JVTextureLoader.set_picked_ball("1006")`.
`apply_picked_cosmetics()` is the single entry the picker uses to apply the current selection.
