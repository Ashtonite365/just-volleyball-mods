
#pragma once
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace jvm {




godot::String install_dir();
godot::String mods_root();




godot::Object *owner_property(godot::Node *from, const godot::String &prop, const godot::String &cls);
godot::String mods_dir(const godot::String &category);


void log(const godot::String &tag, const godot::String &msg);
void warn(const godot::String &tag, const godot::String &msg);
void debug(const godot::String &tag, const godot::String &msg);
int log_level(const godot::String &tag);


godot::Variant load_json(const godot::String &path);

godot::PackedStringArray list_json(const godot::String &dir);

godot::Dictionary merge(const godot::Dictionary &a, const godot::Dictionary &b);

godot::Dictionary load_category_config(const godot::String &category);



godot::TypedArray<godot::Node> find_targets(godot::Node *from, const godot::Variant &spec);



godot::Node *find_gate(godot::Node *from);
bool gate_active(godot::Node *from);
bool gate_cosmetic_active(godot::Node *from);
godot::String gate_reason(godot::Node *from);
bool script_is(godot::Object *o, const godot::String &cls);
godot::Node *current_scene(godot::Node *from);
bool sync_allowed(godot::Node *from);

}
