# Goofy Rules

Rally rules as gameplay settings (ids `goofy.rules.*`, category *rules* in the Settings panel's
`mods` tab). They use the gameplay settings registry ([GAMEPLAY-SETTINGS.md](GAMEPLAY-SETTINGS.md)):

* **offline** (Free Play, practice, offline solo): your values, live,
* **LAN / private / friends lobby**: the host's values. The host's game judges every touch,
  and clients never judge, so the host's rules are the rules,
* **public lobby / quick match**: stock, always.

Every default is the game's own rule, so nothing changes until you change it. The rules are
applied at runtime through CORE (capability `game.rules`) on the host's rally judge. Game
files are never changed.

## The stock rules (what the game does)

| rule | stock behaviour |
|---|---|
| team touches | 3 per side (`Rules.TOUCHES_PER_SIDE`). A 4th touch by the same side is the *four touches* fault: the rally ends and the other team scores. Bots are held back at 3; humans are not, so they get the fault. |
| double hit | a player who touched last cannot touch again until someone else does (the game bars the contact rather than whistling it). A player alone on their side is exempt. |
| blocks | a real block or a body block resets the side's count and the last toucher: it does not count as a touch, and the blocker may play the ball next. |
| practice | no touch rules. |

`game_rules_info(mod)` returns these as the game has them.

## Settings

| id | type | default | what it does |
|---|---|---|---|
| `goofy.rules.double_hit_foul` | bool | on | On: one player may touch at most `max_consecutive_touches` times running. Off: no limit in a row (team touches still apply). |
| `goofy.rules.max_consecutive_touches` | int 1..10 | 1 | How many times running one player may touch while the double-hit foul is on. 1 = stock. |
| `goofy.rules.max_team_touches` | int 1..10 | 3 | Touches a side may make before the ball must cross. One more is the game's *four touches* fault: point to the other team. |
| `goofy.rules.block_classic` | bool | off | The game's classic block switch (`PlayerFeel.block_classic`), labelled "Classic block (no zones, the stuff goes to the cursor)". |

### How the controls work together

* `double_hit_foul` is the switch, `max_consecutive_touches` is the number. With the foul on and
  1 you get stock volleyball. With the foul on and 2 a player may touch twice running and is
  barred on the third. With the foul off the number is ignored.
* Team touches always apply, whatever the double-hit settings say. If
  `max_consecutive_touches` is higher than `max_team_touches`, the team limit comes first.
* Blocks never count, for either limit. A block resets the side's count and the streak, so after
  a block the blocker starts a new streak.
* A side change (the ball crosses and the other team touches) starts a new count and streak.
* A player alone on their side was never barred by the game, and still is not.
* Practice is exempt, as in stock.
* Bots follow the team limit: the game holds them back when the count it sees reaches 3, and
  that count is steered so this happens at the configured limit.
* With the foul on and more than 1 allowed, the game's own last-toucher field is cleared
  while the player is still allowed to touch again. It is set again by the next touch.

### Classic block

Writes `PlayerFeel.block_classic` (stock off) through CORE only when your value differs from
stock. The original value is kept and written back on revert. A client takes the host's value
once the host's settings arrive.

## Fail closed

* Nothing is written while the settings are at stock.
* No rally, a practice rally or a missing game field: nothing happens, the game's own rules run.
* Gameplay closing (public lobby, quick match, the library turned off, the core gate failing)
  reverts at once: the streak is dropped, `block_classic` is restored, and from the next touch
  the game judges with stock rules.
* Clients never touch the rally. The host's game decides every touch.

## Old configs

`max_hits_per_team` (0 = 3) and `no_double_hit` in `mods/goofy/*.json` are imported once into
`max_team_touches` and `double_hit_foul` (inverted). Later edits there are still picked up. The
custom-lobby Modifiers panel rows (max hits, no double-hit) edit the same settings.
