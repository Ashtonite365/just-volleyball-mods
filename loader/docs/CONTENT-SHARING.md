# Cosmetic sharing and auto-download

Capabilities `net.content_offer` and `net.content_download` (access class **sync**: LAN, private and
friends-only lobbies). Implemented in the textures library (`src/textures/content_net.cpp`), with the
pure rules in `src/gate/content_share.*` and the remote-model part in `src/models/model_loader.cpp`.

## What players see

* Every modded player keeps their own cosmetics: outfit, accessories, player model.
* Everyone sees everyone else's outfit, accessories and model, as long as the item is installed or can
  be fetched from the assets repo.
* The court and the ball are shared objects. In a synced lobby every modded client shows the **host's**
  custom court and ball. Your own picks come back when you leave.
* A change (equipping another outfit, picking another ball, switching model) is re-sent to everyone at
  once, also in the middle of a lobby.
* Gameplay values are a separate system (docs/GOOFY-SYNC.md). Cosmetics never change gameplay.

## Offers

A modded peer advertises what it wears in a JVMS packet. It never uses the game's own RPCs.

| type | direction | payload |
|---|---|---|
| 6 `Offer` | client -> host | `u8 n` + n items |
| 7 `OfferOf` | host -> clients | `i32 peer` + `u8 n` + n items |

An item is `u16 id | u8 kind | 32-byte SHA-256 | u32 size`, where kind is 1 outfit, 2 accessory,
3 ball, 4 model or 5 court. There are at most 16 items, and malformed packets are dropped. Older mod
versions ignore types 6 and 7.

The hash is the pack's **content hash**: SHA-256 over the sorted lines
`"<relative path>\n<sha256 of the file>\n<size>\n"` of its PNG / JSON / glb files. It can be computed
both from an installed folder and from the manifest's per-file list, so the same pack gives the same
hash everywhere.

Topology is a host relay, as with the skin-id sync (docs/TEXTURE_SYNC.md):

* When a client joins, the host sends it its own offer and every other peer's offer.
* Each client sends its offer to the host, which relays it to the others.
* Offers are re-checked every 0.5 s and re-sent as soon as they change.

## Downloads

When the assets-repo manifest lists a `download_url` or `source_url` for each file, the loader fetches those files one at a time into `mods/resources/packs/<folder>/` (a sanitized folder name from the manifest). Each URL must be a raw GitHub URL under `https://raw.githubusercontent.com/Ashtonite365/just-volleyball-assets/` with ref `main` or a 40-character commit sha, with no redirects, query strings, or path tricks. Every file is size-capped, SHA-256 checked against the manifest, and run through the existing PNG/JSON/glb content checks. The pack.zip path into `mods/resources/cache/` remains the fallback when per-file URLs are absent. `content_auto_download` in `texture_sync.json` disables both paths.


When a peer wears an item that is not installed, the mod checks the assets repo. Only this repo is
used, and its address is compiled into the build:

    https://raw.githubusercontent.com/Ashtonite365/just-volleyball-assets/main/manifest.json

The manifest is fetched only when someone wears something you don't have. An item is downloaded only
if all of these hold:

* The manifest is valid: `schema_version` 1, a 4-hex `id`, a cosmetic `type` (ball, outfit, hat, face,
  back, model, court / stage) and a 64-hex `sha256`.
* `size` is 1 B to 16 MiB.
* `path` is a plain `folder/name`, and `download_url` is exactly `<repo>/<path>/pack.zip`.
* Every file is a plain relative PNG / JSON / glb name with its own SHA-256 and size, and the files
  total at most 48 MiB.
* The peer's offered hash, size and kind equal the manifest entry. Otherwise the item is a **hash
  mismatch** and is never fetched.
* No installed pack has that id. A local pack always wins.

Then `pack.zip` is fetched with no redirects and a body limit equal to the manifest size. It is
rejected unless its size and SHA-256 equal the manifest's. Each zip entry must pass the extraction
guard:

* No absolute, drive, UNC or backslash paths.
* No `.`, `..`, hidden or empty segments, and no control characters.
* At most 3 levels and 96 bytes per name.
* Only `.png`, `.json` and `.glb` files.

Every entry must also be listed in the manifest with the same size and SHA-256, and every listed file
must be present.

Files are written to `mods/resources/cache/.<folder>.part`, then moved into place as
`mods/resources/cache/<folder>`. The texture and model libraries rescan, and the item appears without
a restart. Code (scripts, scenes, DLLs, pck files) is never downloaded, extracted or run.

**Offline:** if the repo cannot be reached, nothing else changes. Installed and cached packs keep
working, the item shows as "missing (assets repo unreachable)", and the manifest is retried after 60 s.
A failed download is retried in the next session or through `content_fetch`. A rejected one is not
retried.

Set `"content_auto_download": false` in `mods/texture_sync.json` to turn downloads off. Offers are
still exchanged, and missing items are listed.

## Gate

| mode | offers | downloads |
|---|---|---|
| LAN, private, friends-only | yes | yes |
| public lobby, quick match, server browser | no packets | none |
| offline, Free Play, menu | nothing to share | none |

Public lobbies stay local-only for now. Sharing cosmetics with strangers is not enabled. It would
technically be safe, because only ids and hashes are sent and only repo items are downloaded, but it is
left as an owner decision.

## Status

`api.content_status(mod)` (or `JVTextureLoader.get_content_status()`) returns:

* `offer`: what you advertise.
* `peers`: per peer, each item with its state (`have`, `queued`, `downloading`, `installed`,
  `hash mismatch ...`, `missing (...)`, `rejected: ...`).
* `catalog`: `{state, items, errors}`.
* `downloads`, `downloaded`, `rejected`.
* `shown_ball`, `shown_stage`, `cached`.

The models library reports a remote graft per rig in `get_rig_report()` (`remote`) and
`get_remote_models()`.

## Tests

* **Unit** (`tests/test_gate.cpp`): SHA-256 vectors, the content hash, the wire format (round trip,
  truncation, junk, bad kind and version, older-peer compatibility), URL / path rules, manifest
  validation (each rule), the zip-slip guard and download decisions.
* **Net pair `content-share`** (`tests/run_net_pairs.sh`): `tests/tools/mock_assets_server.py` serves a
  loopback copy of the repo. The host wears items the client lacks. The client:
  * fetches and renders an outfit (C001), the ball (C0B1) and a model grafted on the host's rig (B0D1);
  * refuses C0DE (hash mismatch, never fetched), C0FF (tampered `pack.zip`) and C0EE (`../../evil.png`
    entry);
  * keeps exactly the three verified folders in the cache and writes nothing outside it;
  * follows the host's mid-lobby change to C002 through the re-sent offer.
* All other integration runs point the repo at a closed loopback port, so they also cover offline
  behaviour. Only test (debug) builds accept that override (`JV_ASSETS_BASE`, `http://127.0.0.1:` only).
  Release builds always use the embedded repo.
