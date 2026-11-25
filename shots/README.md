# Shots

Unofficial extra-shots library for Just Volleyball: combo shots built from the game's own
actions, including a camera-relative curve.

**This is an unofficial fan project.** It is not affiliated with, endorsed by, or supported
by the developers of Just Volleyball. Original work only. Do not add official game files.

The shot list and input notes are in [docs/SHOTS.md](docs/SHOTS.md).

## Install

1. Install the unofficial core from https://github.com/Ashtonite365/just-volleyball-core.
2. Close Just Volleyball.
3. Back up your existing `mods/bin/libjvm_shots.windows.template_release.x86_64.dll` if you already have one.
4. Copy `dist/bin/libjvm_shots.windows.template_release.x86_64.dll` and `dist/bin/jvm_shots.gdextension` into `mods/bin/`.
5. If you do not already have a shots config, copy `dist/shots/00-default.json` into `mods/shots/`. Keep your own config if you have one.
6. Confirm the DLL hash against `SHA256SUMS.txt`.

## Source

The `src/` tree is the shots library, for building with the core repository. It is not a standalone project.

Related repositories:

- Core / loader: https://github.com/Ashtonite365/just-volleyball-core
- Textures and models: https://github.com/Ashtonite365/just-volleyball-assets
