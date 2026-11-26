



#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

namespace jvm::court_sky {

struct ColorField {
	bool set = false;
	godot::Color color;
};

struct SkySpec {
	bool present = false;
	godot::Ref<godot::Texture2D> panorama;
	float energy = 1.0f;
	bool energy_set = false;
	ColorField ambient;
	ColorField fog;
	float fog_density = 0.012f;
	bool fog_density_set = false;
	bool hide_env_props = false;
	bool hide_ground = false;
	godot::PackedStringArray hide_names;
};



void apply_sky(godot::Node *scene, godot::Node *grass, const SkySpec &sky);
void revert_sky();
godot::Dictionary sky_status();

}
