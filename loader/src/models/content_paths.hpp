#pragma once












#include <godot_cpp/variant/string.hpp>

namespace jvm::content {
godot::String packs_root();
godot::String legacy_models_root();
bool legacy_models_present();
godot::String equipped_path();
godot::String model_pick_read_path();
godot::String model_settings_read_path();
godot::String model_settings_write_path();
}
