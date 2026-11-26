# Extra shots and controls (real game, build 26827091)

All file:line references are to the game's recovered scripts (build 26827091).

## How a shot is applied
* The game's ball is a scripted `Node3D` (`class_name Ball`, game/ball/ball.gd:1-2) that
  integrates its own `vel` (ball.gd:74), `curve` (81), `dive_g` (104) and float
  (`start_float` 1553 / `float_seed_for` 1568). It is not a RigidBody3D.
* Contact is the game's own: `Player._do_touch` (player/player.gd:6119) calls `Ball.hit`
  (ball.gd:1163), which emits `struck(pos, power, kind)` (ball.gd:14) at its end.
* jv-mods hooks `struck` on every `Ball` and moves its handler **first**, so main.gd's
  `_on_ball_struck` (main.gd:1043) and its "strike" net event (`"bv": ball.vel`, ~main.gd:12116)
  carry our velocity. The shot is chosen in that handler from what the hitter is doing.
* Only when: hitter == the local player of the scene that owns the ball (main.gd:217
  `player`, menu.gd:74, or main embedded in the practice court, practice_court.gd:99),
  `Player.sim_authority` (player.gd:935) is true, and the ball is not a net puppet /
  remote-driven (ball.gd:217/221). Online clients skip; the host decides.
* Serve context = `Player.serving` (player.gd:339) seen within the last 0.6 s.
* Topspin -> the game's gravity multiplier `dive_g` (no spin in the game); floats -> the
  game's own float; curve -> `Ball.curve` (ball.gd:944-953: accel = UP x dir * curve).

## Key map (no new keys; the game's own actions)
Game actions and defaults: project.godot [input] (lines 79-238) + core/input_binds.gd:29-70.
Because jv-mods reads the **action names**, rebinds (input_binds.gd:175-191 writes them into
InputMap) and controllers carry over. Per-device couch tables (core/device_input.gd
`bindings`) are not read: in local multiplayer with several pads, combos use the merged state.

| shot | combo | keyboard default | controller default |
|---|---|---|---|
| high shot | rally: hold sprint + strafe, make a **set** touch | Ctrl + Shift + E | LB + LT + X |
| low shot | rally: hold sprint + strafe, **bump/spike** touch | Ctrl + Shift + left click | LB + LT + RT |
| topspin | rally, **airborne** touch with **forward held** (forward component of the game's move vector > 0.5) | hold W, jump, touch | stick up, A, touch |
| v1.0.4 dump | an airborne SET with forward held is the game's own **dump** (`Player.TouchKind.DUMP`); topspin (and any forward-holding combo) yields to it: the game's dump launch is kept, nothing is unbound | hold W, jump, set | stick up, A, set |
| topspin spike boost (v1.0.4) | the topspin above on a touch the game calls a **spike** (`Player.TouchKind.SPIKE`), airborne, forward held: horizontal speed x `goofy.topspin_spike_boost` (default **1.12**, 1..1.3, reset to default in Settings > mods), still on the cursor, dip kept within the plain topspin's own tiers (<= 6 x gravity; else a partial boost / none, logged). Never on regular spikes, sets, dumps (v1.0.4's air-forward setter dump), bumps, any serve shot (jump serve topspin too) or a topspin inside a match's 0.6 s serve contact window. On the solo courts (Free Play / practice) the hit of your own toss is played as a rally touch, so a topspin spike there is boosted. Computed where the launch is (host / offline); clients get the ball state | hold W, jump, spike | stick up, A, spike |
| Curveball | rally, **airborne** touch with **left or right held** (not forward; stronger than back) | hold A / D, jump, touch | stick left/right, A, touch |
| Float | rally, **airborne** bump / spike / dump with **back held** (not forward; at least as strong as left/right). Not on a set: an airborne back set stays the game's | hold S, jump, touch | stick down, A, touch |
| standing float | serve: hold sprint + move back, serve hit on the ground | Ctrl + S + click | LB + stick down + RT |
| float serve | serve: hold sprint + move back, serve hit in the air | Ctrl + S + Space, click | LB + stick down + A, RT |
| high serve | serve: hold sprint, tap the serve hit (charge < 40 %) | Ctrl + tap click | LB + tap RT |
| low serve | serve: hold sprint, medium charge (40-95 %) | Ctrl + click (medium) | LB + RT (medium) |
| fast serve | serve: hold sprint, full charge (>= 95 %) | Ctrl + hold click | LB + hold RT |
| jump serve | serve: hold sprint, serve hit in the air | Ctrl + Space, click | LB + A, RT |
| bend on a shot above (the airborne "curve") | hold the keys that walk you left / right (in your hitting frame) on an **airborne** touch: bends that way | A / D | stick left/right |

### Direction table (what each direction does)
"Forward / back / left / right" below are **where the keys walk you**, read in your hitting frame (see
"Relative directions" further down): toward the net / away from it / to your left / right while facing
the net or your aim. W/S/A/D are only the keys for that with the camera behind you; with the side-on
broadcast camera D is forward, A back, W left and S right.
| input (game action) | ground touch | airborne rally touch | serve |
|---|---|---|---|
| forward (move_forward: W / stick up) | walks; game's own shot, **no mod effect** | **topspin** (+ curve if left/right too) | game's own (no mod combo uses forward) |
| back (move_back: S / stick down) | game's own shot | **Float** = the game's own float (sets: the game's back set) | game's own float; with sprint held: our standing/float serve (unchanged) |
| left / right (move_left/right: A D / stick) | game's own shot and curve, **no mod effect** | **Curveball** (alone) or curve on topspin / sprint+strafe shots | game's own; curve on our sprint serves only when airborne (jump serve) |
| nothing held | game's own shot | game's own shot | game's own serve |

Forward is **never unbound**: W / stick up walk normally (an old `mods/controls.json` is ignored).

Defaults: sprint Ctrl / pad button 9 (project.godot:233), strafe Shift / LT axis 4 (170),
set E / pad 2 (135), contact left mouse / RT axis 5 (129), jump Space / pad 0 (123),
move_back S / stick axis 1+ (97), move_left/right A/D / stick axis 0 (79/85),
move_forward W / stick axis 1- (91).

### Air controls and landing indicators
One air intent per airborne rally touch: **forward > back > left/right** (a diagonal back-left /
back-right picks the stronger axis; forward + back cancel = the game's shot). Holding sideways
in the air can never fire both the Curveball and the bend: the Curveball needs left/right
WITHOUT forward, the bend only rides another mod shot (topspin = forward, or sprint+strafe).

| air input | shot | landing indicator |
|---|---|---|
| forward | topspin | the game's own **exact** landing marker (ball.gd:546-635 on our launch) |
| back | Float: the game's float exactly as its jump float serve (charge = inverse_lerp(FEEL.float_shake_from, FEEL.float_power_cap, power), `Ball.float_seed_for`, `Ball.solve_float` to the aim point, `stop_float` + `start_float`) | the game's own **float-serve box** (it sets `float_serve`, ball.gd:601-635), sent to the other machines with the seed ("fs", main.gd:12119) |
| left / right | Curveball (hold-to-distance) | **spin display** (any shot with `Ball.curve != 0`, including topspin-with-curve). Default `spin_display: "question"`: every client draws the same float uncertainty box from the crosshair to the max landing, with the game's float-serve down-arrow near the true landing (FLOAT_MARK_* fade, small wobble). `spin_display: "exact"` is the same shared box without the arrow. No hitter-only path. `curveball.range_box: false` = the game's exact marker in exact mode. |

The display is visual only and is drawn independently on every machine after the Ball's own
`_physics_process` (process priority 1000), including when a remote shot's landing marker is
placed by the game's own prediction (there is no dedicated or host-only "server view"). It
stops on the next touch (`strike_seq`), on landing or if the ball starts a float. Plain shots
(`curve == 0`) and float serves are not touched.

Every client, including the player who made the shot, draws the **same** uncertainty box
from the same code path and the same box values (crosshair to farthest possible landing
when aim is known, otherwise already-synced ball vel / curve / predicted landing, then
frozen for the rest of the flight). There is no hitter-only path and no remote-only
tint: the game's own float uncertainty box (same corner triangles, styling, wobble and
`FLOAT_MARK_*` fade) with the game's float-serve down-arrow anchored near the true predicted landing in
`spin_display=question`. `curve_display_remote=off` hides that shared box on every
client; `grey` and `full` both use the float-box colour so everyone sees the identical
indicator. The exact landing ring is hidden so it cannot leak the landing. These options
live in the shots config (`shots.spin_display`, `shots.curve_display_remote`); they are
visual-only and are not host-synced gameplay settings.


### No-clash table
| combo element | what the game does with it | why it is safe |
|---|---|---|
| sprint + strafe (rally) | the game resolves the chord to plain strafe: `sprint_held = ... and not strafe_held` (input_collector.gd:379) | sprinting alone (running into the ball) never fires a shot |
| sprint alone (rally) | normal running | **not used** for rally shots |
| sprint (serve) | no effect while standing to serve | serve-only |
| strafe (serve) | pressing it drops the serve charge (player.gd:3660) | **not used** for serves |
| set / bump / spike | the game's own touch kinds (player.gd:11) | used as the game decides them, never held as a modifier |
| charge / jump on the serve | the game's own serve power / jump serve | read from `struck` power and `grounded()` |
| move back | the game's own float tilt on a jump serve (player.gd:7869-7871 `_float_serve_intent`, 6286-6292); on an airborne rally spike it only makes a light spike (player.gd:6192-6195) | airborne rally bump/spike/dump: our Float (the game's own float recipe) replaces the light spike; sets, ground and serves untouched |
| move left / right | the game's own curve (ball.gd:1195 `curve = -influence.x * curve_strength`) | only on airborne touches, where jv-mods' Curveball / camera curve replaces it |
| move forward | walking; the game's own drive tilt | only on airborne rally touches (topspin); ground = game |
| callouts 1-8, chat, stats, camera, zoom, recenter, free mouse, dive/block, serve | own effects on press | never used (checked by `combo_game_clash`, unit tests) |

Ordinary play (any touch without sprint+strafe, any serve without sprint) is always the
game's own shot.

## Camera-relative directions (mirrors the game's movement code)
The game turns the four move actions into a world direction in `InputCollector.collect`
(core/input_collector.gd:313-314) and `_camera_relative_move` (499-509):
```
raw   = _dev.get_vector("move_left", "move_right", "move_forward", "move_back")  # x = right, y = back
fwd   = -camera.global_basis.z ; fwd.y = 0 ; fwd = fwd.normalized()
right =  camera.global_basis.x ; right.y = 0 ; right = right.normalized()
world = right * raw.x + fwd * -raw.y
```
`camera` is the scene's `CameraRig.camera` (main.gd:220 `camera_rig`, menu.gd:71). jv-mods does
exactly this (`shot_math.cpp camera_relative_move`, tested under yaws 0/37/90/180/-135 deg):
* `raw` comes from `Input.get_vector` over the same four actions, so rebinds, sticks and the
  actions' deadzones carry over; W+S cancel like in the game.
* forward intent = `-raw.y > 0.5`; side = `sign(raw.x)` when `|raw.x| > 0.5`.
* the camera is the owner scene's `camera_rig.camera` (fallback: the viewport camera).
* airborne = `Player.grounded()` false (player.gd:942-943: `is_on_floor()` locally, the synced
  flag for puppets) - the same on-floor state the float serve's `_float_serve_intent` uses.

Curve: `world_side = right * raw.x`, projected on the flight's left axis (ball.gd:944-953 bends
toward `UP x dir` for curve > 0): `Ball.curve = amount * dot(UP x flight, world_side)`
(`curve_amount` 12 = BallPhysics.curve_strength, ball_physics.gd:313). Ground touches keep the
game's own `Ball.curve`.

## Relative directions and topspin to the cursor
* **One mapping for every custom shot direction: a key means the world direction it
  would walk you.** The game turns WASD into walking in `InputCollector.collect`
  (input_collector.gd:313-314): `raw = get_vector(move_left, move_right, move_forward, move_back)`,
  `input.move = _camera_relative_move(rig.camera, raw)` (499-509: `fwd = -camera.basis.z` and
  `right = camera.basis.x`, both flattened and normalised, `world = right*raw.x + fwd*-raw.y`; no
  snapping, no side flip: the camera basis alone decides). For its own shot influence the game reads
  the same keys in the hitter's frame (`input.move_raw = _hitter_frame_stick(...)`, 409 and 470-493,
  used by `Player._air_influence`, player.gd:7893): `net_dir` = toward the aim when it is more than
  0.75 m away (SHOT_FRAME_MIN, `shot_frame_tilt`), else `(0, 0, -team_side)`; `net_right = net_dir x UP`
  (that function also snaps the camera look to the nearest of the four frame axes).
  jv-mods (`shot_math hitter_frame_raw / hitter_net_dir`, `JVExtraShots::frame_of`): `world` from
  `_camera_relative_move`, then `x = world . net_right`, `y = -(world . net_dir)`, and the usual
  intent thresholds (one deviation from the game: the aim only tilts the frame when it is over the net, so the
  serve-toss aim `Ball.action_toss_aim`, beside you on your own half, cannot turn D into "back", and during a serve (ball in hands, toss, the 0.6 s serve contact window) the frame is
  always your team's; forward `-y > 0.5` = topspin, back `y > 0.5` and `y >= |x|` = Float, side
  `|x| > 0.5` = Curveball / bend). So with the side-on BROADCAST camera (at +x looking -x) D walks you
  toward the net and is topspin, A is Float; W / S are your left / right. On the other court half
  `team_side` flips `net_dir` with it. The Curveball push, the bend on topspin and Float left / right
  all go along `net_right * side` (the side those keys walk you), not the camera's right.
* **Float left / right**: target = cursor + net_right * side * `float.side_lateral` (default 3 m,
  optional key), then pulled back to the straight float's range from the contact and launched with the
  same `Ball.solve_float` recipe, so height and length match a straight float. Before, the side key was
  ignored (the float landed on the cursor).
* **Curveball**: the cursor is clamped into the court box first and the push then runs along that side
  direction, shortened at the box edge, so the direction is never bent by a per-axis clamp.
* **Topspin lands on the cursor** (`shot_math solve_topspin`): horizontal speed 1.25 x the game's
  normal shot (`Ball.solve_launch` to the same cursor), apex 0.4 m above the normal shot's apex,
  raised in 0.1 m steps until the ball crosses the net plane at least 0.15 m above the net top
  (`PHYS.net_height + PHYS.radius`); the faster arc's dip is the game's own `Ball.dive_g`, capped at
  2.5 (= the old canned topspin's cap and under the game's own synced-dive clamp `PHYS.serve_dive_max`,
  main.gd:7523): when the dip would be bigger the shot trades speed (down to 1.02x) and rise (down to
  +0.1 m) first, then allows a dip up to 6 at 1.02-1.1x speed, and only if even that cannot clear the
  net (a steep spike from deep to a cursor just over the net) goes slower than the game's shot. The
  launch adds 0.5*g*dt to vy so the game's semi-implicit step (ball.gd step_flight) follows the
  solved parabola (net clearance is checked on that parabola; never a net ball). The
  aim is then corrected against the game's own `_predict_land_curved()` (12 passes, < 2 cm), so the
  game's own landing marker sits where the ball lands. Before, a canned 16 m/s, 12 deg launch landed
  a fixed 2.5-5 m past the cursor. `get_last_topspin()` reports target / predicted / speed / apex /
  net clearance; jv_mods.log logs them per shot.

## Curveball spin bar + exponential bend
* **Even bend (the default, `curveball.even_bend: true`).** The curve is the same all
  through the flight (the ramp's `k` is 0: `curve(t) = sgn * A`, solved the same way), and the bar is the
  matching parabola `p(s) = c + f*F*s + d*L*s^2` (starts forwards, bends evenly, a constant-curve
  track's shape). The hold lands at `s = sqrt(hold)`, so the sideways part is `L * hold` (linear: hold 0.38 = 2.28 m, full = 6 m), forwards `F * sqrt(hold)`. Same far end, same cursor
  start, same spread, indicator, clamp, gating as below. `even_bend: false` = the exponential bend below.
* **Path.** From the cursor at the touch `c` (clamped into the court box), forwards along the hit `f`
  (contact -> cursor) and toward the spin side `d` (the side your keys walk you, hitting frame):
  `p(s) = c + f*F*s + d*L*g_k(s)`, `g_k(s) = (e^(k s) - 1) / (e^k - 1)` (`g = s` for k ~ 0), s in 0..1.
  `F = curveball.bar_forward` (1.5 m), `L = curveball.max_lateral` (6 m), both shrunk together until every
  point of the path is inside the court box. `k = k_min + (k_max - k_min) * hold`, `k_min = 0.3`
  (tap: g(0.5) = 0.46, almost straight), `k_max = 4.0` (full hold: g(0.5) = 0.12, the bend saved for the
  far end). hold = the game's hit charge (`struck` power / `hold_power()`, as before).
* **Where it lands.** `p(hold)`: a tap on the cursor, a full hold on the max-curve end, in between
  further along the bar the longer you hold. With the bar on, the float-style spread is only ACROSS the
  path (`spread_short * scale`); along it the hold picks the point.
* **The ball follows it.** In flight the curve is set every step (after the Ball's own step, process
  priority 1000) to `sgn * A * e^(k * t / T)`: it starts going forwards and the bend grows exponentially
  toward the landing (`T` = the straight shot's flight time, same `k`). `A` and the launch aim are
  solved together (`shot_math solve_ramp`: secant on A for the bend's share `L*g_k(hold)`, damped Newton
  on the aim) against our copy of `Ball._predict_land_curved` that steps the game's own static
  `Ball.step_flight` (ball.gd:2239) with that schedule, then launched with the game's `Ball.solve_launch`.
* **Indicator.** Default (`spin_display: "question"`): every client draws the same thin
  uncertainty box from the crosshair to the max landing, using float-serve corner shards
  at native size and aspect (they are not stretched with the box) and a waypoint down-arrow
  (`res://game/fx/waypoint_arrow.gdshader`, same quad size as `_land_arrow`) over the true
  landing. The overlay lives on our own nodes and materials; the game's landing marker,
  meshes and shader parameters are left alone. The box is oriented along the shot
  (local Z = start→end) and sits on the court. `spin_display: "exact"` is the same box
  without the arrow. Tint, shimmer, fade-out (FLOAT_MARK_*) and a small wobble
  (`Ball.float_offset`) match the float box. Our nodes are freed when the shot ends,
  the ball lands, the rally resets or the next shot starts.
* Drawn on every machine that can see the ball's `curve` (the ramp still runs where the shot is
  computed: the authoritative machine). `curveball.range_box: false` = the game's exact marker in
  exact mode. `curve_display_remote=off` hides the shared box on every client; grey and full use the same float-box colour.

## Curveball (hold-to-distance + float-like uncertainty)
* charge01 = the game's hit charge: `struck` power (= `hold_power()` = hold_time / FEEL.max_hold,
  player.gd:3434, 5966 for spikes; set power for sets); for a bump (power = receive quality) it
  reads `hold_power()`.
* target = aim_point + spin_dir * charge01 * `max_lateral` (6 m), clamped to the aim's court half
  (`Ball.court_center_x / court_half_w / court_half_l`, ball.gd:21-25; player.gd:4697-4705 uses
  the same fields). A tap moves it a few cm, a full hold to the sideline / end line.
  The cursor is clamped into that box first and the push then runs along the camera
  side direction itself, shortened at the box edge, so the direction is never bent (a per-axis
  clamp would squash a sideways camera push into "toward / away from the net").
* quality q: 1.0 on the game's perfect spike (`last_perfect`, player.gd:5957), else the game's
  `_receive_quality()` (player.gd:7564: movement speed, ball heat, strafe), else 0.7.
* spread scale = (1 + `quality_spread`*(1-q)) * (1 + `extreme_spread`*max(0, |2c-1| - `extreme_from`)/(1-`extreme_from`)).
* landing = target + spin_dir * u * `spread_long` * scale + across * v * `spread_short` * scale, with
  u = h0+h1-1, v = h2+h3-1 and h_i = the game's float hash `Ball._fhash(seed, i)` (ball.gd:1579,
  bit-exact, checked against Godot 4.7.2), seed = `randi() % 999983 + 1` per hit like the game's
  float pattern (player.gd:6291-6292). Only the authoritative machine computes it (sim_authority,
  not net_puppet / remote_driven), so the ball state the game syncs is the one result.
  The landing is not clamped: a sloppy full hold can land out.
* the ball is launched with the game's own `Ball.solve_launch(from, landing, kind, power, false)`
  (ball.gd:1012) plus `Ball.curve` toward the spin side; the aim point is corrected (up to 20 passes, best kept, step halved when a pass gets worse; `JV_SHOTS_DEBUG=1` logs each pass)
  against the game's own curved landing prediction `_predict_land_curved()` (ball.gd:646) so the
  bent ball lands on the sampled point.
* all constants live in `"curveball": {...} (old key "side_spin" still read)` in mods/shots/00-default.json and in the mods
  menu's shots settings page.

## Config (mods/shots/*.json)
```
{"backend":"jv_ball", "arm_window_s":2.0, "curve_amount":12.0,
 "shots": {"high_shot": {"speed":11, "elevation_deg":55,
   "combo": {"trigger":"touch|off", "hold":["sprint","strafe"], "context":"rally|serve",
             "touch":"any|set|not_set", "air":"any|ground|air", "min_power":0, "max_power":1, "curve":true}}}}
```
A combo that holds a game action with its own effect is logged as a warning at load.

## Serve window
* A touch within 0.6 s of `Player.serving` counts as a serve touch. In a match, a rally shot inside that
  window is skipped and logged (`serve contact in a match (<mode>) ...`, counted in `skipped`).
* Free Play / practice (gate mode `Free Play` / `practice`): every touch is your own toss, and on the
  shared ball the game keeps `Player.serving` true until the contact. So on a solo court a rally combo
  fires on a serve contact whenever no sprint serve combo matched (sprint still gives the serve shots).
  On the Free Play own ball (own_balls.gd:228/253: serving = held, false on the toss;
  `Player.shared_ball_off`) an airborne-only rally shot (topspin, Curveball) fires when no serve combo
  matched (log: `free play own-ball toss: <shot> used as a rally touch`).
* Topspin = the forward component held at the moment of an airborne rally touch (forward is never unbound).
  It replaces the game's solved launch with its canned 16 m/s / 12 deg launch (+ dive_g).
* Activation is tested with real key events through the game's InputMap (`tests/shot_real_input.sh`:
  serve key, toss, walk under the ball, jump, hold the direction, contact; it passes only when the shots
  mod logs `applied <shot>` for a real airborne touch).

## Custom shot debugger (F7)
Off by default. **F7** toggles a translucent panel (top-left) showing live: court mode and whether
shots are ON or blocked (gate), the game actions you are holding (with their keys), the move vector
and air intent (FORWARD = topspin, BACK = float, LEFT/RIGHT = Curveball), AIRBORNE/grounded,
SERVING / serve-contact window time left / rally, shared or own ball, contact state, **"your touch
now -> <shot | game's own shot | BLOCKED: reason>"**, the last applied/skipped event and the last 8
touches (time, kind, AIR/ground, power, serve contact or rally, held keys -> result). The same lines
go to `jv_mods.log` as `[jv-mods:shotdbg]` (toggle, every touch, state changes; max 10/s).
Config (`mods/shots/00-default.json`): `"debugger": {"key": "KEY_F7", "start_visible": false,
"events": 8, "log": true}`.
