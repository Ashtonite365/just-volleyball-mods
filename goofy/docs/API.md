# Goofy API 1.2 — contract for other mods and the unified loader

The Goofy library (`jvm_goofy`) owns the **ball library** (docs/GOOFY-BALL.md, 156 params), the
**player library** (docs/GOOFY-PLAYER.md, 195 params) and **host sync** (docs/GOOFY-SYNC.md).
Mods should use the CORE facades (the CORE API doc in just-volleyball-core (`docs/API.md`), `goofy_*` functions with capabilities
`goofy.query`, `goofy.ball`, `goofy.player`, `goofy.ball_live`, `goofy.events`); this file is the
library-side contract those facades forward to. Every game access Goofy itself makes goes through
CORE as mod `goofy` (`jvm_goofy_io()`), so strict mode and the gate rules cover it.

## Where it lives
* Node `/root/JVGoofyMods` (class `JVGoofyMods`). Through CORE: `api.goofy_available(mod)`.
* Soft dependency: when Goofy is missing, CORE facades return neutral values and the schema still
  comes from CORE's compiled table (both libraries).

## Versioning
* `api_version() -> String` = `"1.2"`. 1.0 functions are unchanged; 1.2 only adds.
* Pack / override dicts may carry `"version": 1` (the goofy *block* schema version). Unknown higher
  versions are applied best-effort: known keys apply, unknown keys are ignored and logged.
* In `status.json`, `libraries.goofy.api` is the CORE API version Goofy registered with
  (currently `"registered (1.3)"`). `libraries.goofy.goofy_api_version` is this Goofy contract
  (`"1.2"`). The loader reports the same contract as
  `libraries.loader.subsystems.goofy_bridge.goofy_api_version`.
* Gameplay settings are stored in `mods/api/state/gameplay/goofy.json`. `values` only lists
  settings that differ from stock defaults; `{ "values": {}, "version": 2 }` is normal when
  everything is at default.

## Query functions (always safe, any mode)
| function | returns |
|---|---|
| `api_version()` | `"1.2"` |
| `is_enabled()` | library on (full gate tier open) |
| `is_physics_allowed()` | values live now: offline modes, or LAN / private / friends lobbies (the host's gameplay values during a match) |
| `get_schema()` | both libraries: `{id, section, library, type, is_color, target, prop, default, min, max, unit, apply}` |
| `get_ball_schema()` / `get_player_schema()` | one library |
| `get_param(id)` | effective value now (stock if nothing overrides it) |
| `get_params()` | `{id: effective value}` for every param that differs from stock |
| `get_stock(id)` | the game's own value (read before any edit) |
| `get_layers()` | `{owner, priority, scope, params}` in precedence order (debug) |
| `get_force_defaults()` | Ball override FIXED **and** a loader map is current |
| `get_ball_mode()` / `get_loader_map()` | `"normal"` / `"fixed"`; current loader map id or `""` |
| `list_balls()` / `get_ball_state(i)` | live ball state (docs/GOOFY-BALL.md) |
| `list_players()` / `get_player_state(i)` | live player state incl. animation (docs/GOOFY-PLAYER.md) |
| `get_player_cosmetics(i)` | read-only cosmetics state |
| `get_sync_status()` | `{role, authoritative, live, host_left, host_values, time_scale, time_scale_synced, peers, sent, received, rejected, seq, last_error, version}` |

## Write functions
Layer writes return `{ok, applied, ignored, errors, active}` (`active` = live right now; a layer set
while gated off is kept and goes live when allowed).

| function | meaning |
|---|---|
| `apply_pack_params(pack_id, params, version = 1)` | the loader: a pack's `goofy` block (owner `pack:<id>`, priority 200, scope stage). Implies `set_loader_map` |
| `clear_pack_params(pack_id)` | remove that pack's layer. Idempotent |
| `set_loader_map(pack_id)` | the loader: current map (`""` = stock map). Drives FIXED; drops other stage-scoped layers |
| `set_override(owner, params, priority = 500, scope = "session")` | a mod layer (CORE uses owner `mod:<name>`, ball ids only). priority 0..899 |
| `set_player_override(owner, params, priority, scope)` | player-library ids only (CORE uses owner `mod:<name>:player`) |
| `clear_override(owner)` | remove that layer. Idempotent |
| `set_ball_mode(mode)` | `"normal"` / `"fixed"`, saved to `mods/goofy/state/ball_override.json` |
| `set_ball_velocity(i, v)` / `set_ball_curve(i, c)` / `apply_ball_impulse(i, dv)` | live ball writes, offline only (finite values, speed <= 200, curve -100..100) |

Signals: `params_changed(changed, effective)`, `layers_changed()`, `ball_mode_changed(mode,
force_defaults)`, `physics_allowed_changed(allowed)`, `game_event(kind, name, index, args)` (ball and
player signals), `sync_changed(role, host_values)`. CORE relays them as bus events
`goofy_params_changed`, `goofy_ball_mode_changed`, `goofy_physics_allowed_changed`,
`goofy_ball_event`, `goofy_player_event`, `goofy_sync_changed`.

## Param dictionaries (the `goofy` block)
Keys are param ids from the schema, e.g. `ball.gravity`, `player.move_speed`. A value is an absolute
number (or `[r,g,b,a]` for colours) or `{"mul": f}` = stock x f. Out-of-range values are clamped and
reported in `errors` (the clamped value is still applied). Aliases: `gravity_scale` (= `ball.gravity`
mul), `bounce_scale` (= `ball.floor_bounce` mul), `big_ball` (= `ball.radius` mul), `player_scale`
(= `player.body_scale`). `_comment` / `version` / keys starting with `_` are skipped.

Example court pack block (test fixture `tests/fixtures/packs/goofy_court/skin.json`, original assets):
```json
"goofy": {
  "version": 1,
  "ball.gravity": { "mul": 0.5 },
  "flight.air_drag": 0.3,
  "player.reach": 1.6,
  "not.a.param": 1
}
```
`apply_pack_params("1090", block, 1)` ->
`{ok: true, applied: ["ball.gravity","flight.air_drag","player.reach"], ignored: ["not.a.param"], errors: [], active: <live?>}`.

## Precedence (low -> high, per param)
1. stock game value
2. user Goofy config `mods/goofy/*.json` (owner `user`, priority 100)
3. map / court pack block (owner `pack:<id>`, priority 200)
4. other mods (default 500; 101..199 sits under the court)
5. **FIXED** (priority 1000, ball library only): on a loader map with Ball override = FIXED every
   ball-library param is stock.

A higher layer replaces (does not stack with) a lower one for the same param; equal priority -> the
later registration wins. During a synced match the client uses the host's gameplay values instead of this
stack (visual-only params still come from it; docs/GOOFY-SYNC.md); the stack comes back at match end.

## Where values are live
* Offline (Free Play, practice, offline solo, menu): the stack above.
* LAN / private / friends: own stack in the lobby; during a match the host's gameplay values and time scale for everyone (visual-only params stay each player's own).
* Public / quick match / server browser: stock, nothing synced.
Rules features (max hits / no double hit, host-enforced) keep their full-tier behaviour.

## Fallback when Goofy is absent / off
If `/root/JVGoofyMods` is missing the loader skips `goofy` blocks silently and loads the pack. When the
node exists it is always told the loader map and pack block (cheap, idempotent); Goofy only pushes
values into the game where allowed, so a block set while gated off goes live by itself later.
