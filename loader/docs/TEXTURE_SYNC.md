# Texture packs and ID sync

## Packs (`mods/resources/packs/<pack>/`)
`skin.json` `{"id": "BEEF", "name": "Beef ball", "kind": "global" | "skin"}` - `id` is
exactly 4 hex digits (case-insensitive, stored upper-case). Content: `res/` mirror of the
game paths and/or `texture_map.json` (`{"res://...png": "file.png"}`), optional `add/`.
Duplicate IDs: the first pack (sorted by folder name) wins and the log gets
`WARNING id BA11 collision: 'dupe' ignored, 'ballpack' kept`.
`mods/resources/packs/equipped.json` `{"equipped": ["BEEF", "CAFE"]}` = what you wear.

* `global` packs replace textures for everyone you see (RenderingServer texture swap).
* `skin` packs are per player: material overrides on that player's meshes, matched by
  the albedo texture's res path. Players come from `Main._peer_players` (peer -> Player,
  main.gd:497) and the local `player`; names from `Net.players` `{peer: {name, sid}}`.

## Sync (IDs only, never files)
Channel: raw `SceneMultiplayer.send_bytes` (the game never uses raw packets; the game's
own RPCs are untouched). Packets are `"JVMS"` + version 1 + type + little-endian payload
(HELLO, WELCOME, EQUIP, EQUIP_OF, LEAVE; max 32 IDs, names <= 64 bytes); malformed or
unknown packets are ignored, clients only trust peer 1 (the host).
* joining modded client -> host: HELLO + EQUIP (retried every 2 s, 5 times)
* modded host -> joiner: WELCOME + EQUIP_OF for every existing player
* modded host -> all other modded clients: EQUIP_OF for the joiner
* the host only ever sends to peers that said HELLO, so **unmodded peers get nothing**
  (tested: unmodded client received 0 jv packets). Unmodded host: Godot drops the raw
  packets (no handler), tested no errors.
* **Fallback** when the host doesn't answer (unmodded host): Steam lobby member data key
  `jvm_skins` = comma-separated IDs, read for every lobby member. Only works on Steam
  lobbies (not plain IP/LAN without Steam); not covered by the automated tests (they run without Steam).

Chosen: host relay primary, lobby member data fallback.

For each player wearing an ID you don't have, a line at the bottom of the screen:
`You're missing BEEF from Bob` - that player keeps the default texture.

## Gate
Textures are the **cosmetic tier**: allowed everywhere the full tier is, plus public
custom lobbies, Quick Match, server browser and the idle main menu. OFF in unknown states and on a
version mismatch. Shots/goofy stay OFF in public/quick/browser.

**ID sync only runs while the full gate is ON** (mode LAN, friends-only or private/password only - `jvm::sync_allowed`; the main menu is local only;
offline modes have no network). In public lobbies, Quick Match, server browser and any
unknown state textures are **purely local**: no packets sent or answered (`send_to`,
`_on_peer_packet` and the lobby-member-data tick all re-check the gate), no lobby member
data written or read, no "missing" lines, and skins learned from other players are
dropped. If a lobby goes public mid-session, sync stops in the same/next frame
(tested: 1 frame) and the log says `sync: local only (blocked: public lobby ...)`.
The one exception: if our IDs were already published as lobby member data in the
private phase, that key is cleared (set to "") once on the switch so it doesn't linger
in a now-public lobby. status.json `texture_sync.local_only` carries the reason.

## Model packs (`kind: "model"`)
`skin.json` with `"kind": "model"` belongs to jvm_models (docs/MODELS.md). It shares the
4-hex id namespace (duplicate ids are rejected). Models are not part of the skin-id list: select one
with `equipped.json` `"model": "AE01"`, not in `"equipped"` (the texture loader would send ids
listed there to other players as skin ids). The model you wear reaches other players through
content offers (docs/CONTENT-SHARING.md), which also carry outfits, accessories, the ball and the court.
