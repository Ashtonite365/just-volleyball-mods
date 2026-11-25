# Goofy host sync

**Rule:** in **LAN, private and friends-only lobbies**, everything that alters the ball or a player
follows the **host** during a match, unless it is visual-only. **Public lobbies, quick match and the
server browser never sync.** There the CORE gate keeps everything local-only, and Goofy physics stay
stock on every machine.

## What follows the host
| class | what | during a synced match |
|---|---|---|
| gameplay | every ball and player param that is not visual-only: hitboxes (`ball.radius`, `player.reach`, block radii, ...), gravity (`ball.gravity`, `player.gravity`), drag (`flight.air_drag`, `flight.vertical_drag_scale`, `ball.roll_drag`), bounce, spin / curve / float, speed caps, serve, shot shaping, movement, jump, sprint / stamina, dive / fall, block, animation timing, contact timing, `player.body_scale` | **the host's values** (stock where the host is stock) |
| time scale | `Engine.time_scale`, as set by the host's Goofy `time_scale` (1.0 when the host has none) | **the host's value** |
| visual-only | the `visuals` sections and every colour param: trail, outline, contest band, rings, indicator, stamina / dive colours | **your own** |
| cosmetics | outfit, accessories, ball skin, model, court textures | **your own** (shared, not forced: docs/CONTENT-SHARING.md) |

Every param's class is listed in docs/GOOFY-BALL.md and docs/GOOFY-PLAYER.md (column `sync`). It is
also returned by `goofy_ball_schema` / `goofy_player_schema` (`sync`: `host` / `local`). The rule is:
a param is visual-only when it is a colour or sits in a `visuals` section. Everything else is gameplay.

Each client applies the host's values and simulates locally, so ball prediction (landing marker,
solvers) runs with exactly the host's numbers and time scale, without corrections or hiccups.

There is no separate space-platform gravity or underwater drag, in the game or in Goofy. The space
stage changes visuals and footstep sound only. Gravity and drag are the ordinary gameplay params
above, and they follow the host. Generic `overrides` (arbitrary node properties) are never synced.

## When
| role | when | values in play |
|---|---|---|
| `offline` | Free Play, practice, offline solo, menu | your own layers (config, court pack, mods) |
| `host` | you host a LAN / private / friends lobby | your own layers; gameplay values are published to modded peers |
| `client`, in the lobby | joined a LAN / private / friends lobby, no match running | your own layers |
| `client`, match | from **match start** (or lobby practice) to **match end** | **the host's gameplay values + time scale**; your own visual-only params |
| `off` | public / quick match / unknown, or Goofy off | stock |

* **Match start** means the game's `Net.in_match` or `Net.practice_live` turns on (read through CORE
  `get_net_state`). The host broadcasts at that moment, on every change, and as a 5 s heartbeat.
* **Match end** (`in_match` / `practice_live` off), **host leaves** (peer 1 disconnects), **leaving the
  lobby** or **the lobby turning public:** the client's own values and its own time scale come back at
  once. After the host leaves, the client stays on its own values until the next session.
* **The game's hit-stop:** the game's own short slow-mo (`GameConfig.slow_time`) sets
  `Engine.time_scale` back to exactly 1.0 when it ends. Goofy then re-asserts the time scale in force,
  so prediction keeps the same rate.
* Rules features (max hits, no double hit) keep their own host-enforced path (lobby key `jvm_rules`).

## Protocol (SceneMultiplayer peer packets, CORE capability `net.packets`)
* client -> host: `JVGH` + `{"v":2}` (hello). Sent every 2 s until the first host payload, then every 5 s,
  so a host that restarted or reloaded its config re-learns its modded peers.
* host -> client: `JVGF` + JSON `{"v":2,"sid":<host session>,"seq":<n>,"ts":<time scale>,"params":{"<id>": number}}`.
  It carries the host's gameplay values that differ from stock, as absolute values only.
* The host builds the payload in one function (`gameplay_snapshot`). It is the single place that decides
  what is sent.
* Unmodded players never receive anything, and the game's own RPCs are never used.

## Receive rules (client)
* Packets are accepted only from peer 1 (the host), and only while this machine's role is `client`.
  Values received in the lobby are kept and take effect at match start.
* A packet is at most 32 KB and must be a JSON object. `v` must be 2, and `seq` must grow within a host
  session (`sid`). Replays are ignored.
* `ts` must be a finite number. It is clamped to 0.1 .. 4.0, the Goofy `time_scale` range.
* Every key must be a known **gameplay** param id. Visual-only ids are dropped. Numbers must be finite
  and are clamped to the param's range; integer params are rounded. Multipliers, arrays and any other
  value type are dropped. Dropped values are counted (`rejected`) and logged once per packet.
* The payload is **data only**. It is parsed as JSON and turned into numbers. Nothing received is ever
  executed, loaded, evaluated or used as a path.

## Status
`api.goofy_sync_status(mod)` returns `{role, authoritative, live, host_left, host_values, time_scale,
time_scale_synced, peers, sent, received, rejected, seq, last_error, version}`. The bus event is
`goofy_sync_changed {role, host_values}`. Goofy's `get_status()` carries the same under `features.sync`.

## Tests
* **Unit** (tests/test_gate.cpp): value validation / clamping, the visual / gameplay classification
  (gravity, drag, bounce, hitbox, speed caps, jump and body scale are gameplay), time-scale clamping,
  and host mode in the layer stack (visual params stay local; host values are kept for the next match
  and cleared when the host leaves).
* **Integration** (gameplay):
  * Lobby, then match start, then match end.
  * Crafted host packets for every category, including the time scale.
  * A visual id is dropped.
  * Rejected packets: from a non-host peer, an old version, a bad time scale, oversize, bad JSON.
    Replays and non-numeric values are ignored or dropped.
  * The time scale is re-asserted after a game hit-stop.
  * Lobby practice follows the host. Host leaving restores your values.
  * A public lobby is ignored, even in a match.
* **Net pairs** (tests/run_net_pairs.sh: real ENet on localhost, `JV_NET_PORT` picks the port,
  `JV_PAIRS` picks pairs). Host and client run different configs.
  * In LAN and private, the client keeps its own values in the lobby.
  * At match start it matches the host's gravity, hitbox, drag, bounce, spin, movement, jump, body
    scale and time scale, and keeps its own outline colour.
  * It follows a mid-match host change, and gets its own values and time scale back at match end.
  * In a public lobby, no Goofy packet is sent or received, also during a match.
