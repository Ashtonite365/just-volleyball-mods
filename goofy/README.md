# Goofy

Unofficial gameplay library for Just Volleyball: ball and player parameter libraries, host sync, and the settings that live under the Mods panel (ball size, gravity, game speed, and match rules).

**This is an unofficial fan project.** It is not affiliated with, endorsed by, or supported by the developers of Just Volleyball. Original work only. Do not add official game files.

The library contract is documented in [docs/API.md](docs/API.md). This library is Goofy API **1.2**. It registers with CORE API **1.3**.

## Install

1. Install the unofficial core from https://github.com/Ashtonite365/just-volleyball-core.
2. Close Just Volleyball.
3. Back up your existing `mods/bin/libjvm_goofy.windows.template_release.x86_64.dll` if you already have one.
4. Copy `dist/bin/libjvm_goofy.windows.template_release.x86_64.dll` and `dist/bin/jvm_goofy.gdextension` into `mods/bin/`.
5. If you do not already have a Goofy config, copy `dist/goofy/00-default.json` into `mods/goofy/`. Keep your own config if you have one.
6. Confirm the DLL hash against `SHA256SUMS.txt`.

`mods/api/state/gameplay/goofy.json` stores only settings that differ from the stock defaults. An empty `values` object is normal when everything is at default.

## Source

The `src/` tree is the Goofy library and its parameter tables, for building with the core repository. It is not a standalone project.

Related repositories:

- Core / loader: https://github.com/Ashtonite365/just-volleyball-core
- Textures and models: https://github.com/Ashtonite365/just-volleyball-assets
