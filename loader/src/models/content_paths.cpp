#include "content_paths.hpp"

#include "../common/jvm_common.hpp"
#include "../textures/texture_loader.hpp"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/variant/dictionary.hpp>

using namespace godot;

namespace jvm::content {

String packs_root() { return jvm::mods_dir(texture_pack_rel()); }
String legacy_models_root() { return jvm::mods_dir("models"); }
bool legacy_models_present() { return DirAccess::dir_exists_absolute(legacy_models_root()); }
String equipped_path() { return packs_root().path_join("equipped.json"); }

String model_pick_read_path() {
	String merged = equipped_path();
	Variant v = jvm::load_json(merged);
	if (v.get_type() == Variant::DICTIONARY && Dictionary(v).has("model")) return merged;
	String old = legacy_models_root().path_join("equipped.json");
	if (FileAccess::file_exists(old)) return old;
	return merged;
}

String model_settings_write_path() { return packs_root().path_join("model_settings.json"); }

String model_settings_read_path() {
	String merged = model_settings_write_path();
	if (FileAccess::file_exists(merged)) return merged;
	String old = legacy_models_root().path_join("model_settings.json");
	if (FileAccess::file_exists(old)) return old;
	return merged;
}

}
