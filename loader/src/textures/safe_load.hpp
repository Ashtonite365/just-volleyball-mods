




#pragma once
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/string.hpp>

namespace jvm::safe {

godot::String pack_file(const godot::String &pack_dir, const godot::String &rel, bool images_only = false);

bool no_links(const godot::String &pack_dir, const godot::String &full);

godot::Ref<godot::Image> load_image(const godot::String &path, godot::String &why);

godot::Node *load_glb(const godot::String &path, godot::String &why);

int64_t refused_count();
godot::String last_refusal();
void note_refusal(const godot::String &what);

}
