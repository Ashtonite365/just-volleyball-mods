#include "court_sky.hpp"
#include "../common/jv_prof.hpp"

#include <godot_cpp/classes/environment.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/panorama_sky_material.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/sky.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/classes/world_environment.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include <set>
#include <vector>

using namespace godot;

namespace jvm::court_sky {
namespace {

struct PropBak {
	uint64_t id = 0;
	StringName prop;
	Variant previous;
};
struct VisBak {
	uint64_t id = 0;
	bool was_visible = true;
};

std::vector<PropBak> sky_props_;
std::vector<VisBak> sky_vis_;
bool sky_on_ = false;

static bool name_is(const String &n, const char *want) {
	return n.casecmp_to(String(want)) == 0;
}

static bool is_env_prop(const String &n) {

	return name_is(n, "Hills") || name_is(n, "Trees") || name_is(n, "GrassField")
		|| name_is(n, "WorldFill") || name_is(n, "Coast") || name_is(n, "City")
		|| name_is(n, "Crowd") || name_is(n, "Town") || name_is(n, "Audience")
		|| name_is(n, "Spectators");
}

static bool is_ground(const String &n) {
	return name_is(n, "EnvFloor");
}

static bool in_hide_list(const String &n, const PackedStringArray &names) {
	for (int i = 0; i < names.size(); i++) {
		if (n.casecmp_to(names[i]) == 0) return true;
	}
	return false;
}

static void hide_named(Node *n, bool env, bool ground, const PackedStringArray &names, int depth, std::set<uint64_t> &seen) {
	if (!n || depth > 14) return;
	uint64_t id = n->get_instance_id();
	if (seen.count(id)) return;
	seen.insert(id);
	String nm = n->get_name();
	bool hide = (env && is_env_prop(nm)) || (ground && is_ground(nm)) || in_hide_list(nm, names);
	if (hide) {
		if (Node3D *n3 = Object::cast_to<Node3D>(n)) {
			sky_vis_.push_back({ id, n3->is_visible() });
			n3->set_visible(false);
		}
		return;
	}
	int cc = n->get_child_count();
	for (int i = 0; i < cc; i++) hide_named(n->get_child(i), env, ground, names, depth + 1, seen);
}

static void collect_world_envs(Node *n, std::vector<WorldEnvironment *> &out, int depth, std::set<uint64_t> &seen) {
	if (!n || depth > 16) return;
	uint64_t id = n->get_instance_id();
	if (seen.count(id)) return;
	seen.insert(id);
	if (WorldEnvironment *we = Object::cast_to<WorldEnvironment>(n)) out.push_back(we);
	int cc = n->get_child_count();
	for (int i = 0; i < cc; i++) collect_world_envs(n->get_child(i), out, depth + 1, seen);
}

}

void apply_sky(Node *scene, Node *grass, const SkySpec &sky) {
	JVM_PROF("court_sky.apply");
	revert_sky();
	if (!sky.present) return;

	std::set<uint64_t> seen;
	std::vector<WorldEnvironment *> envs;
	if (scene) collect_world_envs(scene, envs, 0, seen);
	if (grass) collect_world_envs(grass, envs, 0, seen);
	if (scene && scene->get_tree() && scene->get_tree()->get_root())
		collect_world_envs(scene->get_tree()->get_root(), envs, 0, seen);

	for (WorldEnvironment *we : envs) {
		if (!we) continue;
		Ref<Environment> orig = we->get_environment();
		sky_props_.push_back({ we->get_instance_id(), "environment", orig });
		Ref<Environment> env;
		if (orig.is_valid()) env = orig->duplicate();
		else env.instantiate();
		if (env.is_null()) continue;
		env->set_background(Environment::BG_SKY);
		if (sky.panorama.is_valid()) {
			Ref<PanoramaSkyMaterial> mat;
			mat.instantiate();
			mat->set_panorama(sky.panorama);
			mat->set_energy_multiplier(sky.energy_set ? sky.energy : 1.0f);
			Ref<Sky> sk;
			sk.instantiate();
			sk->set_material(mat);
			env->set_sky(sk);
		}
		if (sky.ambient.set) {
			env->set_ambient_source(Environment::AMBIENT_SOURCE_COLOR);
			env->set_ambient_light_color(sky.ambient.color);
			env->set_ambient_light_energy(1.0f);
		}
		if (sky.fog.set) {
			env->set_fog_enabled(true);
			env->set_fog_light_color(sky.fog.color);
			env->set_fog_density(sky.fog_density_set ? sky.fog_density : 0.012f);
		}
		we->set_environment(env);
		sky_on_ = true;
	}

	if (sky.hide_env_props || sky.hide_ground || sky.hide_names.size() > 0) {
		std::set<uint64_t> hseen;
		if (scene) hide_named(scene, sky.hide_env_props, sky.hide_ground, sky.hide_names, 0, hseen);
		if (grass) hide_named(grass, sky.hide_env_props, sky.hide_ground, sky.hide_names, 0, hseen);
		if (scene && scene->get_parent())
			hide_named(scene->get_parent(), sky.hide_env_props, sky.hide_ground, sky.hide_names, 0, hseen);
	}
}

void revert_sky() {
	for (auto it = sky_props_.rbegin(); it != sky_props_.rend(); ++it) {
		if (Object *o = ObjectDB::get_instance(it->id)) o->set(it->prop, it->previous);
	}
	sky_props_.clear();
	for (auto it = sky_vis_.rbegin(); it != sky_vis_.rend(); ++it) {
		if (Node3D *n = Object::cast_to<Node3D>(ObjectDB::get_instance(it->id)))
			n->set_visible(it->was_visible);
	}
	sky_vis_.clear();
	sky_on_ = false;
}

Dictionary sky_status() {
	Dictionary d;
	d["applied"] = sky_on_;
	d["hidden"] = int(sky_vis_.size());
	d["envs"] = int(sky_props_.size());
	return d;
}

}
