# Pack content is data only

Every pack (installed by hand, from GitHub, or auto-downloaded for another player's cosmetic) is
treated as hostile. A pack can carry PNG / JPEG / WebP images, binary glTF (`.glb`) meshes and
JSON. Nothing a pack contains or names can run code.

## Rules (src/gate/safe_content.cpp, src/textures/safe_load.cpp)

| what | rule |
|---|---|
| paths a pack names (skin.json, texture_map.json, preview, env/lod meshes, accessories, models) | plain relative path inside the pack: no `res://` `user://` or any scheme, no absolute / drive / UNC path, no `..`, no hidden segments, no `\ : * ? " < > \| %`, max 200 chars / 8 levels, no symlink on the way |
| file types | images `png jpg jpeg webp`, meshes `glb`, data `json`. `.tres .res .tscn .scn .gd .gltf .svg .tga .bmp .import .gdshader .dll` etc. are refused |
| images | read as bytes; magic bytes must match the extension; header size <= 8192 px per side and <= 8192x4096 pixels, file <= 32 MB, checked **before** decoding; decoded with `Image::load_{png,jpg,webp}_from_buffer`; decoded size must equal the header |
| meshes | GLB only (magic, version 2, length, chunk bounds), <= 64 MB. Refused: any `uri` on buffers or images (external files and `data:`), images other than embedded PNG/JPEG within caps, `extensionsRequired` outside the allow-list. Stripped: `extras`, every extension outside the allow-list (`KHR_materials_unlit`, `KHR_materials_emissive_strength`, `KHR_materials_clearcoat`, `KHR_texture_transform`, `KHR_mesh_quantization`, `GODOT_single_root`), unknown chunks. The rebuilt GLB is loaded with `GLTFDocument::append_from_buffer` and an **empty base path** (nothing external can resolve) and `HANDLE_BINARY_EMBED_AS_UNCOMPRESSED` |
| player models | `.glb` only. `.tscn/.scn/.gltf` scenes are refused and `"allow_scripts"` is ignored (warning) |
| global texture replacement targets | must be a game texture path: `res://`, not under `mods/` or `.godot/`, no `..` or `//`, image extension; loaded with the `Texture2D` type hint |
| JSON | Godot `JSON` (no objects, no `str_to_var`), files > 4 MB ignored; no pack field is used as a class name, script, callable or shader |
| downloads | unchanged: embedded https repo only, no redirects, manifest SHA-256 + size for the zip and every file, entry-name guard (zip-slip), png/json/glb only, written only under `mods/resources/cache/<folder>`. New: every extracted file must also pass the checks above (magic bytes / header / GLB sanitiser / JSON) or the whole pack is rejected |

Pack content never reaches `ResourceLoader`. Refusals are logged as `[jv-mods:security] refused: ...`
and counted in the textures status (`security_refused`, `security_last_refusal`).

## Tests

* Unit: `g++ -std=c++17 -Isrc tests/test_safe_content.cpp src/gate/safe_content.cpp src/gate/content_share.cpp -o /tmp/tsc && /tmp/tsc`
  (spoofed images, oversize / bomb headers, GLB external + data URIs, extensions / extras,
  bounds, deep JSON, traversal ids, zip-slip names, game texture targets, shipped GLBs still pass).
* Integration (`loader` and `models` modes): `tests/tools/make_hostile_packs.py` adds packs with a
  `.tres` whose embedded GDScript sets `Engine` meta `jvm_pwned`, referenced through `res://`, `user://`,
  an absolute path, a spoofed `.png`, a `texture_map.json` target and a GLB image URI; `..` paths;
  a real 20000x20000 PNG bomb. The canary must never run and every refusal must be logged.

## Court environment lights (`environment.lights`)

`skin.json` can ask for omni lights around a custom court. This is data only:
each entry gives `type` (only `"omni"` is used, anything else is ignored),
`position`, `color`, `energy` and `range`. The loader always creates a plain
`OmniLight3D` with shadows off; no class name, script, node path or resource
path comes from the pack. Limits: at most 8 lights per pack (the rest are
dropped and logged), entries with non-finite numbers are refused, energy is
clamped to 0..16, range to 0..100, colour channels to 0..4 and the position
to +/-500 on each axis.

## Native audio (not in this branch)

`feat/native-audio` loads sounds through `ResourceLoader`. Before any pack
can name a sound file it must switch to `AudioStreamOggVorbis.load_from_buffer`
/ `AudioStreamWAV.load_from_buffer` / `AudioStreamMP3` data from a capped,
magic-checked buffer, or a pack could ship `x.ogg.import` remapping to a
`.tres` with a script.
