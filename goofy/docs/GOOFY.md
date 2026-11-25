# Goofy mods (mods/goofy/*.json) - full tier only
| feature | hooked to (evidence) | default | how |
|---|---|---|---|
| big_ball | BallPhysics.radius (ball_physics.gd:9, shared `static var PHYS = preload(...)` ball.gd:10) + `Ball` node scale only for what the game's own model fit (ball.gd:845, sized from PHYS.radius at dress time) is missing | **1.0 (off)** | resource edit (real hitbox); visual = hitbox, never double-scaled; reverted on gate off |
| ball_physics | same shared BallPhysics resource: `gravity_scale` x gravity (ball_physics.gd:11), `bounce_scale` x floor_bounce, or any exported property by name | **1.0 / 1.0 (off)** | live resource edit, originals restored |
| player_scale | every `Player` (CharacterBody3D, player.gd:1-2) | 1.0 (opt-in) | node scale |
| max_hits_per_team | Rally.touch_count/touch_side (rally.gd:155-156), judged in judge_touch (rally.gd:898; "four touches" fault 985-994) via Player.touched (player.gd:126, emitted 6458 when sim_authority) connected after main.gd:967 | 0 = off (game rule 3) | after each judged touch the host steers touch_count so the game's own THREE_TOUCH bar (rules.gd:71) and four-touches fault land on touch N; the fault label stays "four touches" |
| no_double_hit | Rally.last_toucher (rally.gd:157) -> Rules.bar_reason CONSECUTIVE (rules.gd:62-64) | off (opt-in) | host clears last_toucher after each touch and every physics frame (before pre_tick, rally.gd:590-610); touches still count toward max hits; the game has no double-hit *fault*, only the CONSECUTIVE bar |
| time_scale | Engine.time_scale | opt-in (not in the default file) | your own value offline and in a lobby; in a LAN / private / friends match the host's value (docs/GOOFY-SYNC.md) |
| overrides | generic reversible property overrides, selectors path / group / script_class / scene_property | none | |

Host-authoritative: rules apply where `Net.active` is false or `Net.is_host` (bars reach
clients through the player state flags, player.gd:8746/8871). Clients only display.

## Modifiers panel rows
The game's custom-lobby `ModifiersPanel` (ui/lobby/modifiers_panel.gd; created by
menu.gd:3851-3866 `_open_modifiers`, named "ModifiersPanel") gets two extra rows built
with its own `_entry_row` (modifiers_panel.gd:153-177) and `_rule` (144-150), so they look
like the existing rows: "max hits per team" (choice: off, 1..10) and "no double-hit foul"
(toggle). We listen to the panel's `changed` signal; our keys reach menu.gd's
`_on_room_rule` / `_on_local_rule` too, where unknown keys are harmless.
The rows are hidden unless the full gate is on (hidden in public lobbies / Quick Match).
Sync: the host publishes `mh=N;ndh=0|1` in its own Steam lobby data key `jvm_rules`
(the game's "rules" key, net.gd:4224, has a fixed schema); clients show those values.

## Ball size
The game fits the ball's visual model to `PHYS.radius` when a ball is dressed (ball.gd:845), so a ball created after
a radius edit is already the new size; scaling its node as well would double the effect. So all goofy values default
to neutral (nothing touched), 1.0 multipliers are skipped, and the node scale is `radius_now / radius_dressed`,
measured per ball every 0.5 s (handles set_ball_skin re-dress). The game's own F10 hitbox view
(fx/hitbox_view.gd:247,300) draws from PHYS.radius, so it only grows when big_ball is explicitly set.
