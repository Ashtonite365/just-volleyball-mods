# Unified loader

Unofficial texture, model, and stage-pack loader for Just Volleyball (`jvm_loader`). It applies
resource packs and custom player models at runtime and can run a stage pack's optional `goofy`
section when Goofy is installed.

**This is an unofficial fan project.** It is not affiliated with, endorsed by, or supported by the
developers of Just Volleyball. Original work only. Do not add official game files.

The library contract is documented in [docs/API.md](docs/API.md). This library is Loader API **1.0**.
It registers with CORE API **1.3**.

## Install

1. Install the unofficial core from https://github.com/Ashtonite365/just-volleyball-core.
2. Close Just Volleyball.
3. Build this library (see Source below), then copy `jvm_loader.gdextension` from `dist/bin/` and
   the built `libjvm_loader` binary into `mods/bin/`.
4. Packs and models come from https://github.com/Ashtonite365/just-volleyball-assets and install
   into `mods/resources/packs/`. Example config files are in that repository's `samples/` folder
   (`picker.json`, `equipped.json`, `texture_sync.json`).

Core's bootstrap loads `jvm_loader` when the `.gdextension` file is present, and skips it when it
is not.

## Source

The `src/` tree is the loader library and the gate/common files it links. Build with godot-cpp
`godot-4.5-stable`:

```
tools/fetch_godot_cpp.sh
scons platform=linux target=template_debug -j8
scons platform=windows use_mingw=yes target=template_release -j8
```

Related repositories:

- Core: https://github.com/Ashtonite365/just-volleyball-core
- Textures and models: https://github.com/Ashtonite365/just-volleyball-assets
