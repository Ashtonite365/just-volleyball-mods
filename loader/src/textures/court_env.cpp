#include "safe_load.hpp"
#include "court_env.hpp"
#include "../common/jvm_common.hpp"
#include "../common/jv_prof.hpp"
#include "../common/api_client.hpp"
jvm::GameIO &jvm_textures_io();

#include <godot_cpp/classes/base_material3d.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/geometry_instance3d.hpp>
#include <godot_cpp/classes/gltf_document.hpp>
#include <godot_cpp/classes/gltf_state.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/light3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/omni_light3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/collision_object3d.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/core/object.hpp>

#include <set>
#include <vector>

using namespace godot;

namespace jvm::court_env {
namespace {

const char *SWAY_SHADER = R"(
shader_type spatial;
render_mode cull_disabled, depth_draw_opaque, specular_disabled, shadows_disabled;
uniform vec4 albedo_color : source_color = vec4(1.0);
uniform sampler2D albedo_tex : source_color, hint_default_white;
uniform bool use_tex = false;
uniform float sway_amp = 0.18;
uniform float sway_speed = 0.55;
void vertex() {
	float h = max(VERTEX.y, 0.0);
	float w = clamp(h / 5.0, 0.0, 1.0);
	float p = VERTEX.x * 0.7 + VERTEX.z * 0.5 + NODE_POSITION_WORLD.x + NODE_POSITION_WORLD.z;
	VERTEX.x += sin(TIME * sway_speed + p) * sway_amp * w;
	VERTEX.z += cos(TIME * sway_speed * 0.82 + p * 0.73) * sway_amp * 0.55 * w;
}
void fragment() {
	vec4 t = use_tex ? texture(albedo_tex, UV) : vec4(1.0);
	ALBEDO = t.rgb * albedo_color.rgb * COLOR.rgb;
	// Opaque on purpose: assigning ALPHA puts the pass in the transparent
	// queue and buries the net, court lines, and the landing indicator.
}
)";

struct VisBak {
	uint64_t id = 0;
	bool was_visible = true;
};

struct Instance {
	uint64_t parent_id = 0;
	uint64_t root_id = 0;
	godot::String parent_path;
	int spawned = 0;
	int64_t tris = 0;
	int draws = 0;
	std::vector<VisBak> vis;
};

std::vector<Instance> instances_;
int64_t polish_tris_ = 0;
int polish_draws_ = 0;

static String node_path_of(Node *n) {
	if (!n) return String("(null)");
	if (n->is_inside_tree()) return String(n->get_path());
	return String(n->get_name());
}

static bool name_is(const String &n, const char *want) {
	return n.casecmp_to(String(want)) == 0;
}




static bool is_env_prop(const String &n) {
	String t = n.strip_edges();
	if (name_is(t, "Hills") || name_is(t, "Trees") || name_is(t, "GrassField")
			|| name_is(t, "WorldFill") || name_is(t, "Coast") || name_is(t, "City")
			|| name_is(t, "Crowd") || name_is(t, "Town") || name_is(t, "Audience")
			|| name_is(t, "Spectators") || name_is(t, "Backdrop") || name_is(t, "Sunlight")
			|| name_is(t, "FloorGleam") || name_is(t, "Lights") || name_is(t, "Top floor")
			|| name_is(t, "GymMats") || name_is(t, "Ambient") || name_is(t, "TreeCards")
			|| name_is(t, "TreeShadows") || name_is(t, "Ocean") || name_is(t, "LandBase")
			|| name_is(t, "Palms") || name_is(t, "PalmCards") || name_is(t, "PalmShadows"))
		return true;
	if (t.begins_with("Hill") || t.begins_with("Seats") || t.begins_with("Gym walls")
			|| t.begins_with("Ceiling") || t.begins_with("Gym doors") || t.begins_with("GymExtra")
			|| t.begins_with("Tree") || t.begins_with("Palm") || t.begins_with("Bld")
			|| t.begins_with("Shadow") || t.begins_with("FillShadow")
			|| t.begins_with("Stand") || t.begins_with("BeachCrowd") || t.begins_with("BeachTown")
			|| t.begins_with("CrowdField") || t.begins_with("NationalsStand"))
		return true;
	return false;
}
static bool is_ground(const String &n) { return name_is(n.strip_edges(), "EnvFloor"); }
static bool is_kit_container(const String &n) {
	String t = n.strip_edges();
	return name_is(t, "School") || name_is(t, "Gym") || name_is(t, "Gymnasium")
		|| name_is(t, "Grass") || name_is(t, "Beach");
}
static bool is_keep_play(const String &n) {
	String t = n.strip_edges();
	return name_is(t, "Court") || name_is(t, "CourtFloor") || t.begins_with("Net")
		|| t.begins_with("JVM_") || t.begins_with("Jvm") || name_is(t, "Main")
		|| name_is(t, "Stage") || name_is(t, "Preview") || name_is(t, "Ball");
}
static bool in_hide_list(const String &n, const PackedStringArray &names) {
	for (int i = 0; i < names.size(); i++)
		if (n.casecmp_to(names[i]) == 0) return true;
	return false;
}

static void hide_named(Node *n, bool env, bool ground, const PackedStringArray &names, int depth, std::set<uint64_t> &seen, std::vector<VisBak> &vis) {
	if (!n || depth > 18) return;
	uint64_t id = n->get_instance_id();
	if (seen.count(id)) return;
	seen.insert(id);
	String nm = String(n->get_name());


	if (is_keep_play(nm) || Object::cast_to<Camera3D>(n)) {
		if (name_is(nm.strip_edges(), "Court") || String(nm).begins_with("JVM_") || String(nm).begins_with("Jvm"))
			return;
		int cc = n->get_child_count();
		for (int i = 0; i < cc; i++) hide_named(n->get_child(i), env, ground, names, depth + 1, seen, vis);
		return;
	}
	bool hide = (env && is_env_prop(nm)) || (ground && is_ground(nm)) || in_hide_list(nm.strip_edges(), names);



	if (!hide && env && !is_kit_container(nm)
			&& Object::cast_to<MeshInstance3D>(n)) {
		for (Node *p = n->get_parent(); p; p = p->get_parent()) {
			if (is_kit_container(String(p->get_name()))) { hide = true; break; }
			if (is_keep_play(String(p->get_name()))) break;
			if (Object::cast_to<Viewport>(p)) break;
		}
	}
	if (hide) {
		if (Node3D *n3 = Object::cast_to<Node3D>(n)) {
			vis.push_back({ id, n3->is_visible() });
			n3->set_visible(false);
		}
		return;
	}
	int cc = n->get_child_count();
	for (int i = 0; i < cc; i++) hide_named(n->get_child(i), env, ground, names, depth + 1, seen, vis);
}





static Node *pick_env_parent(Node *scene, Node *court, Node *grass) {
	auto named = [](Node *n, const char *want) {
		return n && String(n->get_name()).strip_edges().casecmp_to(String(want)) == 0;
	};
	Node *start = court ? court : grass;
	if (!start) {
		(void)scene;
		return nullptr;
	}




	Node *fallback = start->get_parent() ? start->get_parent() : start;
	for (Node *n = start; n; n = n->get_parent()) {
		if (named(n, "Main") || named(n, "Stage") || named(n, "Preview"))
			return n;
		if (Object::cast_to<Window>(n)) break;
		if (Object::cast_to<Viewport>(n) && !Object::cast_to<Window>(n))
			break;
		if (named(n, "Menu"))
			break;
		if (n != start) fallback = n;
	}
	return fallback;
}

static void restore_vis(std::vector<VisBak> &vis) {
	for (auto it = vis.rbegin(); it != vis.rend(); ++it) {
		if (Node3D *n = Object::cast_to<Node3D>(ObjectDB::get_instance(it->id)))
			n->set_visible(it->was_visible);
	}
	vis.clear();
}

static void drop_root(uint64_t root_id) {
	if (root_id == 0) return;
	if (Node *n = Object::cast_to<Node>(ObjectDB::get_instance(root_id))) {
		if (n->get_parent()) n->get_parent()->remove_child(n);
		n->queue_free();
	}
}

static void revert_instance(Instance &in) {
	drop_root(in.root_id);
	restore_vis(in.vis);
	in.root_id = 0;
	in.spawned = 0;
}

static void revert_for_parent(uint64_t parent_id) {
	for (auto it = instances_.begin(); it != instances_.end();) {
		if (it->parent_id == parent_id) {
			revert_instance(*it);
			it = instances_.erase(it);
		} else ++it;
	}
}

static bool is_floorish_name(const String &n) {
	String t = n.to_lower();
	return t.contains("floor") || t.contains("deck") || t.contains("ground")
		|| t.contains("sand") || t.contains("seabed") || t.contains("terrace")
		|| t.contains("dojo_floor") || t.contains("snow_floor");
}

static bool is_lanternish_name(const String &n) {
	String t = n.to_lower();

	return t.contains("lantern_glass") || t.contains("lamp_glass") || t.contains("glow");
}



static void make_opaque_default(Material *m) {
	if (!m) return;
	if (BaseMaterial3D *bm = Object::cast_to<BaseMaterial3D>(m)) {
		bm->set_transparency(BaseMaterial3D::TRANSPARENCY_DISABLED);
		bm->set_render_priority(0);
	} else if (ShaderMaterial *sm = Object::cast_to<ShaderMaterial>(m)) {
		sm->set_render_priority(0);
	}
}



static void sanitize_env_colliders(Node *n, int depth) {
	if (!n || depth > 16) return;
	if (CollisionObject3D *co = Object::cast_to<CollisionObject3D>(n)) {
		co->set_collision_layer(0);
		co->set_collision_mask(0);
		co->set_ray_pickable(false);
	}
	int cc = n->get_child_count();
	for (int i = 0; i < cc; i++) sanitize_env_colliders(n->get_child(i), depth + 1);
}

static void apply_sway(Node *n, int depth) {
	if (!n || depth > 12) return;
	if (MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(n)) {
		Ref<Shader> sh;
		sh.instantiate();
		sh->set_code(SWAY_SHADER);
		int surfaces = 0;
		if (mi->get_mesh().is_valid()) surfaces = mi->get_mesh()->get_surface_count();
		if (surfaces <= 0) surfaces = 1;
		for (int s = 0; s < surfaces; s++) {
			Ref<ShaderMaterial> sm;
			sm.instantiate();
			sm->set_shader(sh);
			sm->set_shader_parameter("sway_amp", 0.18);
			sm->set_shader_parameter("sway_speed", 0.55);
			sm->set_render_priority(-128);
			Ref<Material> orig = mi->get_active_material(s);
			if (orig.is_valid()) {
				if (StandardMaterial3D *std = Object::cast_to<StandardMaterial3D>(orig.ptr())) {
					sm->set_shader_parameter("albedo_color", std->get_albedo());
					Ref<Texture2D> tex = std->get_texture(BaseMaterial3D::TEXTURE_ALBEDO);
					if (tex.is_valid()) {
						sm->set_shader_parameter("albedo_tex", tex);
						sm->set_shader_parameter("use_tex", true);
					}
				}
			}
			mi->set_surface_override_material(s, sm);
		}
	}
	int cc = n->get_child_count();
	for (int i = 0; i < cc; i++) apply_sway(n->get_child(i), depth + 1);
}

static void polish_mesh(Node *n, bool baked, bool sway_all, float vis_begin, float vis_end, int depth) {
	if (!n || depth > 14) return;
	if (MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(n)) {
		mi->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
		if (vis_end > 0.0f || vis_begin > 0.0f) {
			mi->set_visibility_range_begin(vis_begin);
			mi->set_visibility_range_end(vis_end);
		}
		if (mi->get_mesh().is_valid()) {
			Ref<Mesh> mesh = mi->get_mesh();
			int sc = mesh->get_surface_count();
			polish_draws_ += sc > 0 ? sc : 1;
			for (int s = 0; s < sc; s++) {
				Array arr = mesh->surface_get_arrays(s);
				if (arr.size() > Mesh::ARRAY_VERTEX) {
					PackedVector3Array verts = arr[Mesh::ARRAY_VERTEX];
					PackedInt32Array idx;
					if (arr.size() > Mesh::ARRAY_INDEX)
						idx = arr[Mesh::ARRAY_INDEX];
					if (idx.size() > 0) polish_tris_ += idx.size() / 3;
					else polish_tris_ += verts.size() / 3;
				}
			}
			String nm_early = String(n->get_name()).to_lower();
			const bool lanternish = is_lanternish_name(nm_early);
			if (baked) {
				for (int s = 0; s < sc; s++) {
					Ref<Material> orig = mi->get_active_material(s);
					if (StandardMaterial3D *std = Object::cast_to<StandardMaterial3D>(orig.ptr())) {
						std->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
						std->set_flag(BaseMaterial3D::FLAG_SRGB_VERTEX_COLOR, true);
						std->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
						if (lanternish) {


							std->set_feature(BaseMaterial3D::FEATURE_EMISSION, true);
							std->set_emission(std->get_albedo());
							std->set_emission_energy_multiplier(1.8f);
							std->set_shading_mode(BaseMaterial3D::SHADING_MODE_PER_PIXEL);
						} else {
							std->set_flag(BaseMaterial3D::FLAG_DISABLE_AMBIENT_LIGHT, true);
							std->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
						}
					}
				}
			}
		}

		if (mi->get_mesh().is_valid()) {
			Ref<Mesh> mesh = mi->get_mesh();
			int sc = mesh->get_surface_count();
			for (int s = 0; s < sc; s++) {
				make_opaque_default(mi->get_active_material(s).ptr());
				make_opaque_default(mi->get_surface_override_material(s).ptr());
			}
		}
		make_opaque_default(mi->get_material_override().ptr());
		String nm = String(n->get_name()).to_lower();

		if (!is_floorish_name(nm) && (sway_all || nm.contains("sway") || nm.contains("kelp") || nm.contains("grass")))
			apply_sway(mi, 0);
	}
	int cc = n->get_child_count();
	for (int i = 0; i < cc; i++) polish_mesh(n->get_child(i), baked, sway_all, vis_begin, vis_end, depth + 1);
}







static const int kEnvRenderPriority = -128;
static const float kFloorTopY = -0.06f;

static void force_opaque_low_priority(Material *mat) {
	if (!mat) return;
	if (StandardMaterial3D *std = Object::cast_to<StandardMaterial3D>(mat)) {
		std->set_transparency(BaseMaterial3D::TRANSPARENCY_DISABLED);
		std->set_render_priority(kEnvRenderPriority);
	} else if (ShaderMaterial *sm = Object::cast_to<ShaderMaterial>(mat)) {
		sm->set_render_priority(kEnvRenderPriority);
	}
}

static bool looks_like_floor(MeshInstance3D *mi) {
	if (!mi) return false;
	if (is_floorish_name(String(mi->get_name())))
		return true;
	if (!mi->is_inside_tree()) return false;
	AABB w = mi->get_global_transform().xform(mi->get_aabb());
	const float top = w.position.y + w.size.y;

	return w.size.x > 8.0f && w.size.z > 8.0f && w.size.y < 1.5f
		&& top > -0.25f && top < 0.6f;
}

static void enforce_env_sorting(Node *n, int depth) {
	if (!n || depth > 16) return;
	if (MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(n)) {
		if (mi->get_material_override().is_valid())
			force_opaque_low_priority(mi->get_material_override().ptr());
		Ref<Mesh> mesh = mi->get_mesh();
		int sc = mesh.is_valid() ? mesh->get_surface_count() : 0;
		if (sc <= 0) sc = 1;
		for (int s = 0; s < sc; s++) {
			Ref<Material> ov = mi->get_surface_override_material(s);
			if (ov.is_valid()) {
				force_opaque_low_priority(ov.ptr());
				continue;
			}
			if (!mesh.is_valid()) continue;
			Ref<Material> sm = mesh->surface_get_material(s);
			if (!sm.is_valid()) continue;
			Ref<Material> dup = sm->duplicate();
			force_opaque_low_priority(dup.ptr());
			mi->set_surface_override_material(s, dup);
		}
	}
	int cc = n->get_child_count();
	for (int i = 0; i < cc; i++) enforce_env_sorting(n->get_child(i), depth + 1);
}

static void clamp_env_floors(Node *n, int depth) {
	if (!n || depth > 16) return;
	if (MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(n)) {
		if (looks_like_floor(mi) && mi->is_inside_tree()) {
			AABB w = mi->get_global_transform().xform(mi->get_aabb());
			const float top = w.position.y + w.size.y;
			if (top > kFloorTopY) {
				Vector3 p = mi->get_global_position();
				p.y -= (top - kFloorTopY);
				mi->set_global_position(p);
			}
		}
	}
	int cc = n->get_child_count();
	for (int i = 0; i < cc; i++) clamp_env_floors(n->get_child(i), depth + 1);
}

static Node3D *load_glb(const String &path, String &why) {
	JVM_PROF("court_env.load_glb");
	if (path.is_empty()) { why = "empty path"; return nullptr; }
	if (!FileAccess::file_exists(path)) { why = "file not found"; return nullptr; }
	Node *gen = jvm::safe::load_glb(path, why);
	if (!gen) { jvm::safe::note_refusal("court mesh " + path + ": " + why); return nullptr; }
	Node3D *root = Object::cast_to<Node3D>(gen);
	if (!root) {
		root = memnew(Node3D);
		root->add_child(gen);
	}
	why = "";
	return root;
}

}


static void attach_pack_lights(Node3D *root, const EnvSpec &env) {
	if (!root) return;
	for (const LightSpec &L : env.lights) {
		String typ = L.type.to_lower();
		if (!typ.is_empty() && typ != "omni") continue;
		OmniLight3D *ol = memnew(OmniLight3D);
		ol->set_name("JVM_PackLight");
		ol->set_position(L.position);
		ol->set_color(L.color);
		ol->set_param(Light3D::PARAM_ENERGY, L.energy);
		ol->set_param(Light3D::PARAM_RANGE, L.range);
		ol->set_shadow(false);
		root->add_child(ol);
	}
}

void apply_env(Node *scene, Node *court, Node *grass, const EnvSpec &env) {
	JVM_PROF("court_env.apply");
	Node *parent = pick_env_parent(scene, court, grass);
	if (parent)
		revert_for_parent(parent->get_instance_id());
	else
		jvm::warn("court-env", "no Stage/Grass/court parent (not using get_current_scene()); leaving stock ground visible");

	if (!env.present) return;
	if (!parent) return;

	std::vector<LodSpec> lods = env.lods;
	if (lods.empty() && !env.mesh_path.is_empty()) {
		LodSpec d;
		d.mesh_path = env.mesh_path;
		lods.push_back(d);
	}

	std::vector<std::pair<LodSpec, Node3D *>> loaded;
	for (const LodSpec &lod : lods) {
		String why;
		Node3D *n = load_glb(lod.mesh_path, why);
		if (!n) {
			jvm::warn("court-env", "load_glb failed path=" + lod.mesh_path + " why=" + why
				+ " parent=" + node_path_of(parent));
			continue;
		}
		jvm::log("court-env", "load_glb ok path=" + lod.mesh_path + " parent=" + node_path_of(parent));
		loaded.push_back({ lod, n });
	}

	if (loaded.empty()) {
		jvm::warn("court-env", "no env mesh spawned; stock ground left visible parent=" + node_path_of(parent));
		return;
	}



	Instance in;
	in.parent_id = parent->get_instance_id();
	in.parent_path = node_path_of(parent);
	if (env.hide_env_props || env.hide_ground || env.hide_names.size() > 0) {
		std::set<uint64_t> hseen;
		hide_named(parent, env.hide_env_props, env.hide_ground, env.hide_names, 0, hseen, in.vis);
	}

	Node3D *root = memnew(Node3D);
	root->set_name("JVM_CourtEnv");
	parent->add_child(root);
	in.root_id = root->get_instance_id();
	in.tris = 0;
	in.draws = 0;
	polish_tris_ = 0;
	polish_draws_ = 0;

	for (auto &[lod, n] : loaded) {
		n->set_name(env.name.is_empty() ? lod.mesh_path.get_file().get_basename() : env.name);
		n->set_position(env.position);
		n->set_rotation_degrees(env.rotation_deg);
		n->set_scale(env.scale);
		polish_mesh(n, env.baked_lighting, env.sway, lod.visible_begin, lod.visible_end, 0);
		sanitize_env_colliders(n, 0);
		root->add_child(n);
		in.spawned++;
	}
	attach_pack_lights(root, env);



	enforce_env_sorting(root, 0);
	clamp_env_floors(root, 0);
	in.tris = polish_tris_;
	in.draws = polish_draws_;
	instances_.push_back(in);
	jvm::log("court-env", "applied spawned=" + String::num_int64(in.spawned)
		+ " hidden=" + String::num_int64(int64_t(in.vis.size()))
		+ " lights=" + String::num_int64(int64_t(env.lights.size()))
		+ " parent=" + in.parent_path);
}

void revert_env_for(Node *parent) {
	if (parent) revert_for_parent(parent->get_instance_id());
}

void revert_env() {
	for (auto &in : instances_) revert_instance(in);
	instances_.clear();
}

Dictionary env_status() {
	Dictionary d;
	int spawned = 0, hidden = 0, draws = 0;
	int64_t tris = 0;
	Array inst;
	for (const auto &in : instances_) {
		spawned += in.spawned;
		hidden += int(in.vis.size());
		draws += in.draws;
		tris += in.tris;
		Dictionary one;
		one["parent"] = in.parent_path;
		one["spawned"] = in.spawned;
		one["hidden"] = int(in.vis.size());
		one["root_alive"] = in.root_id != 0 && ObjectDB::get_instance(in.root_id) != nullptr;
		inst.push_back(one);
	}
	d["applied"] = spawned > 0;
	d["count"] = spawned;
	d["tris"] = int(tris);
	d["draws"] = draws;
	d["hidden"] = hidden;
	d["instances"] = inst;
	return d;
}

}
