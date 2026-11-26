



#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <vector>

namespace jvm::court_env {

struct LodSpec {
	godot::String mesh_path;
	float visible_begin = 0.0f;
	float visible_end = 0.0f;
};

struct LightSpec {
	godot::String type = "omni";
	godot::Vector3 position = godot::Vector3(0, 0, 0);
	godot::Color color = godot::Color(1.0f, 0.78f, 0.45f);
	float energy = 3.5f;
	float range = 30.0f;
};

struct EnvSpec {
	bool present = false;
	godot::String mesh_path;
	godot::String name = "env";
	godot::Vector3 position = godot::Vector3(0, 0, 0);
	godot::Vector3 rotation_deg = godot::Vector3(0, 0, 0);
	godot::Vector3 scale = godot::Vector3(1, 1, 1);
	bool hide_env_props = false;
	bool hide_ground = false;
	godot::PackedStringArray hide_names;
	bool baked_lighting = false;
	bool sway = false;
	std::vector<LodSpec> lods;
	std::vector<LightSpec> lights;
};

void apply_env(godot::Node *scene, godot::Node *court, godot::Node *grass, const EnvSpec &env);
void revert_env();
void revert_env_for(godot::Node *parent);
godot::Dictionary env_status();

}
