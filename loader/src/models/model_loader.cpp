#include "model_loader.hpp"
#include "../common/display_avatar.hpp"
#include "textures/safe_load.hpp"
#include "../common/jv_prof.hpp"
#include "model_tab.hpp"
#include "content_paths.hpp"
#include <set>
#include <unordered_map>
#include <unordered_set>

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/json.hpp>

#include <godot_cpp/classes/base_material3d.hpp>
#include <godot_cpp/classes/bone_attachment3d.hpp>
#include <godot_cpp/classes/cylinder_mesh.hpp>
#include <godot_cpp/classes/decal.hpp>
#include <godot_cpp/classes/sphere_mesh.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/geometry_instance3d.hpp>
#include <godot_cpp/classes/gltf_document.hpp>
#include <godot_cpp/classes/gltf_state.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/multiplayer_api.hpp>
#include <godot_cpp/classes/multiplayer_peer.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/scene_state.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/skin.hpp>
#include <godot_cpp/classes/visual_instance3d.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/core/object.hpp>

using namespace godot;
namespace md = jvm::models;
namespace sk = jvm::skins;



static constexpr const char *kRigScript = "res://game/character/model_rig.gd";
static constexpr const char *kTag = "models";
static constexpr const char *kGraftMeta = "jvm_model";

static std::string S(const String &s) { return std::string(s.utf8().get_data()); }
static String G(const std::string &s) { return String::utf8(s.c_str()); }
template <class T> static T *inst(ObjectID id) { return id.is_valid() ? Object::cast_to<T>(ObjectDB::get_instance(id)) : nullptr; }




static godot::JVModelLoader *g_models = nullptr;
static const char *const kModelsCaps[] = { "gate.mode", "config.files", "scene.current", "scene.players", "player.model",
	"player.look", "ui.locker", "ui.style" };
jvm::GameIO &jvm_models_io() {
	static jvm::GameIO io = [] {
		jvm::GameIO g;
		g.mod = "models";
		g.writes_need_open = false;
		for (const char *c : kModelsCaps) g.caps.push_back(c);
		return g;
	}();
	return io;
}
JVModelLoader::JVModelLoader() {
	g_models = this;
	follow_.mod = "models";
	follow_.access = "cosmetic";
	follow_.api_version = "1.1";
	for (const char *c : kModelsCaps) follow_.caps.push_back(c);
	set_process_mode(PROCESS_MODE_ALWAYS);
	set_process_priority(10000);
}
JVModelLoader::~JVModelLoader() { free_remote_templates(); free_template(); if (g_models == this) g_models = nullptr; }

void JVModelLoader::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_on_gate_changed", "active", "reason"), &JVModelLoader::_on_gate_changed);
	ClassDB::bind_method(D_METHOD("_on_access_changed", "access", "open", "reason"), &JVModelLoader::_on_access_changed);
	ClassDB::bind_method(D_METHOD("_on_model_hook", "scene"), &JVModelLoader::_on_model_hook);
	ClassDB::bind_method(D_METHOD("_service"), &JVModelLoader::_service);
	ClassDB::bind_method(D_METHOD("get_remote_models"), &JVModelLoader::get_remote_models);
	ClassDB::bind_method(D_METHOD("reload_config"), &JVModelLoader::reload_config);
	ClassDB::bind_method(D_METHOD("get_status"), &JVModelLoader::get_status);
	ClassDB::bind_method(D_METHOD("get_status_brief"), &JVModelLoader::get_status_brief);
	ClassDB::bind_method(D_METHOD("get_graft_count"), &JVModelLoader::get_graft_count);
	ClassDB::bind_method(D_METHOD("get_rig_report"), &JVModelLoader::get_rig_report);
	ClassDB::bind_method(D_METHOD("get_model_list"), &JVModelLoader::get_model_list);
	ClassDB::bind_method(D_METHOD("get_saved_model"), &JVModelLoader::get_saved_model);
	ClassDB::bind_method(D_METHOD("get_shown_model"), &JVModelLoader::get_shown_model);
	ClassDB::bind_method(D_METHOD("is_previewing"), &JVModelLoader::is_previewing);
	ClassDB::bind_method(D_METHOD("preview_model", "id"), &JVModelLoader::preview_model);
	ClassDB::bind_method(D_METHOD("apply_model", "id"), &JVModelLoader::apply_model);
	ClassDB::bind_method(D_METHOD("end_preview"), &JVModelLoader::end_preview);
	ClassDB::bind_method(D_METHOD("get_model_settings", "id"), &JVModelLoader::get_model_settings);
	ClassDB::bind_method(D_METHOD("set_model_color", "id", "color"), &JVModelLoader::set_model_color);
	ClassDB::bind_method(D_METHOD("reset_model_color", "id"), &JVModelLoader::reset_model_color);
	ClassDB::bind_method(D_METHOD("set_model_option", "id", "key", "value"), &JVModelLoader::set_model_option);
	ClassDB::bind_method(D_METHOD("flush_settings"), &JVModelLoader::flush_settings);
	ClassDB::bind_method(D_METHOD("_on_stock_visibility_changed", "id"), &JVModelLoader::_on_stock_visibility_changed);
}

bool godot::jvm_is_locker(Object *o) {
	if (!o) return false;
	Variant sc = o->get_script();
	Script *s = sc.get_type() == Variant::OBJECT ? Object::cast_to<Script>(sc.operator Object *()) : nullptr;
	return s && s->get_path() == "res://game/character/locker.gd";
}

void JVModelLoader::log_once(const String &key, const String &msg, bool warning) {
	std::string k = S(key) + "|" + S(msg);
	if (logged_.count(k)) return;
	logged_.insert(k);
	if (warning) jvm::warn(kTag, msg); else jvm::log(kTag, msg);
}

const md::Spec *JVModelLoader::spec() const {
	if (!selected_) return nullptr;
	auto it = packs_.find(*selected_);
	if (it == packs_.end() || !it->second.parsed.spec) return nullptr;
	return &*it->second.parsed.spec;
}


static std::optional<std::string> jstr(const Dictionary &d, const char *k, std::vector<std::string> &bad) {
	if (!d.has(k)) return std::nullopt;
	if (d[k].get_type() != Variant::STRING && d[k].get_type() != Variant::STRING_NAME) { bad.push_back(k); return std::nullopt; }
	return S(String(d[k]));
}
static std::optional<bool> jbool(const Dictionary &d, const char *k, std::vector<std::string> &bad) {
	if (!d.has(k)) return std::nullopt;
	if (d[k].get_type() != Variant::BOOL) { bad.push_back(k); return std::nullopt; }
	return bool(d[k]);
}
static std::optional<double> jnum(const Dictionary &d, const char *k, std::vector<std::string> &bad) {
	if (!d.has(k)) return std::nullopt;
	if (d[k].get_type() != Variant::FLOAT && d[k].get_type() != Variant::INT) { bad.push_back(k); return std::nullopt; }
	return double(d[k]);
}

void JVModelLoader::load_packs() {
	packs_.clear();
	warnings_.clear();
	selected_.reset();



	const String root = jvm::content::packs_root();
	const String old_root = jvm::content::legacy_models_root();
	const bool have_root = DirAccess::dir_exists_absolute(root);
	const bool have_old = DirAccess::dir_exists_absolute(old_root);
	if (!have_root && !have_old) model_state_ = "no folder " + root;
	PackedStringArray dirs;
	std::vector<String> full;
	std::vector<sk::PackInfo> infos;
	std::vector<Dictionary> mans;
	std::set<std::string> installed_ids;
	auto scan = [&](const String &base, const String &key_prefix, bool models_only) {
		PackedStringArray ds = DirAccess::get_directories_at(base);
		ds.sort();
		for (int i = 0; i < ds.size(); i++) {
			if (ds[i].begins_with(".")) continue;
			String dir = base.path_join(ds[i]);
			Variant v = jvm::load_json(dir.path_join("skin.json"));
			Dictionary d = v.get_type() == Variant::DICTIONARY ? Dictionary(v) : Dictionary();
			if (models_only && !md::is_model_kind(S(String(d.get("kind", ""))))) continue;
			String key = key_prefix + ds[i];
			dirs.push_back(key);
			full.push_back(dir);
			infos.push_back({ S(key), S(String(d.get("id", ""))) });
			installed_ids.insert(S(String(d.get("id", "")).to_upper()));
			mans.push_back(d);
		}
	};
	if (have_root) scan(root, "", true);
	if (have_old) scan(old_root, have_root ? "models/" : "", false);
	legacy_layout_ = have_old;


	{
		String croot = content_cache_dir();
		PackedStringArray cached = DirAccess::dir_exists_absolute(croot) ? DirAccess::get_directories_at(croot) : PackedStringArray();
		cached.sort();
		for (int i = 0; i < cached.size(); i++) {
			if (cached[i].begins_with(".")) continue;
			String dir = croot.path_join(cached[i]);
			Variant v = jvm::load_json(dir.path_join("skin.json"));
			Dictionary d = v.get_type() == Variant::DICTIONARY ? Dictionary(v) : Dictionary();
			if (String(d.get("kind", "")).strip_edges().to_lower() != "model" || installed_ids.count(S(String(d.get("id", "")).to_upper()))) continue;
			dirs.push_back("cache/" + cached[i]);
			full.push_back(dir);
			infos.push_back({ S(dirs[dirs.size() - 1]), S(String(d.get("id", ""))) });
			mans.push_back(d);
		}
	}

	sk::Registry reg = sk::build_registry(infos);
	std::map<sk::SkinId, md::InstalledPack> installed;
	for (auto &[id, folder] : reg.by_id) {
		int idx = dirs.find(G(folder));
		Dictionary d = mans[idx];
		Pack p;
		p.folder = G(folder);
		p.dir = full[idx];
		p.is_model = md::is_model_kind(S(String(d.get("kind", ""))));
		if (p.is_model) {
			md::RawManifest m;
			m.kind = S(String(d.get("kind", "")));
			m.id = S(String(d.get("id", "")));
			m.name = S(String(d.get("name", p.folder)));
			m.model = jstr(d, "model", m.bad_types).value_or("");
			m.team_tint = jstr(d, "team_tint", m.bad_types);
			m.tint_param = jstr(d, "tint_param", m.bad_types);
			m.game_hands = jstr(d, "game_hands", m.bad_types);
			m.apply_to = jstr(d, "apply_to", m.bad_types);
			m.color = jstr(d, "color", m.bad_types);
			m.hide_base = jbool(d, "hide_base", m.bad_types);
			m.allow_scripts = jbool(d, "allow_scripts", m.bad_types);
			m.flash = jbool(d, "flash", m.bad_types);
			m.tint_strength = jnum(d, "tint_strength", m.bad_types);
			m.max_rest_error = jnum(d, "max_rest_error", m.bad_types);
			if (d.has("arms")) {
				if (d["arms"].get_type() != Variant::DICTIONARY) m.bad_types.push_back("arms");
				else {
					Dictionary a = d["arms"];
					m.has_arms = true;
					std::vector<std::string> bad;
					m.arms_mode = jstr(a, "mode", bad);
					m.arms_anchor_bone = jstr(a, "anchor_bone", bad);
					m.arms_upper = jnum(a, "upper", bad);
					m.arms_lower = jnum(a, "lower", bad);
					auto vec = [&](const char *k) -> std::optional<std::vector<double>> {
						if (!a.has(k)) return std::nullopt;
						std::vector<double> v;
						if (a[k].get_type() != Variant::ARRAY) { bad.push_back(k); return v; }
						Array arr = a[k];
						for (int i = 0; i < arr.size(); i++) {
							if (arr[i].get_type() != Variant::FLOAT && arr[i].get_type() != Variant::INT) { bad.push_back(k); return std::vector<double>(); }
							v.push_back(double(arr[i]));
						}
						return v;
					};
					m.arms_shoulder = vec("shoulder");
					m.arms_radius = vec("radius");
					m.arms_elbow_hint = vec("elbow_hint");
					for (auto &b : bad) m.bad_types.push_back("arms." + b);
				}
			}
			if (d.has("tint_light")) {
				if (d["tint_light"].get_type() == Variant::DICTIONARY) {
					Dictionary tl = d["tint_light"];
					Array k = tl.keys();
					for (int j = 0; j < k.size(); j++)
						if (tl[k[j]].get_type() == Variant::FLOAT || tl[k[j]].get_type() == Variant::INT) m.tint_light[S(String(k[j]))] = double(tl[k[j]]);
				} else m.bad_types.push_back("tint_light");
			}
			p.parsed = md::parse_manifest(m);
			p.manifest = p.parsed.spec;
			for (auto &e : p.parsed.errors) { std::string w = "pack '" + S(p.folder) + "': " + e; warnings_.push_back(w); jvm::warn(kTag, G(w)); }
			for (auto &e : p.parsed.warnings) { std::string w = "pack '" + S(p.folder) + "': " + e; warnings_.push_back(w); jvm::warn(kTag, G(w)); }
		}
		installed[id] = { p.is_model, p.parsed.spec.has_value(), S(p.folder) };
		packs_[id] = p;
	}
	for (auto &w : reg.warnings) warnings_.push_back(w);
	if (!settings_dirty_) load_settings();
	for (auto &[id, p] : packs_) apply_settings(p);
	Variant eq = jvm::load_json(jvm::content::model_pick_read_path());
	std::optional<std::string> model_field;
	std::vector<std::string> equipped;
	if (eq.get_type() == Variant::DICTIONARY) {
		Dictionary d = eq;
		if (d.has("model") && (d["model"].get_type() == Variant::STRING || d["model"].get_type() == Variant::STRING_NAME)) model_field = S(String(d["model"]));
		Variant a = d.get("equipped", Array());
		if (a.get_type() == Variant::ARRAY) { Array arr = a; for (int i = 0; i < arr.size(); i++) equipped.push_back(S(String(arr[i]))); }
	}
	md::EquipResult er = md::select_model(model_field, equipped, installed);
	for (auto &w : er.warnings) { warnings_.push_back(w); jvm::warn(kTag, G(w)); }
	saved_ = er.model;
	selected_ = er.model;
	if (preview_on_) {
		selected_ = preview_id_;
		if (selected_ && !installed.count(*selected_)) selected_.reset();
	}
	int n = 0;
	for (auto &[id, p] : packs_) n += p.is_model ? 1 : 0;
	jvm::log(kTag, String::num_int64(n) + " model pack(s); equipped model: " + (selected_ ? G(sk::format_id(*selected_)) : String("none")));
}

void JVModelLoader::free_template() {
	template_scene_.unref();
	grey_cache_.clear();
	avg_cache_.clear();
	have_template_own_ = false;
	if (template_node_) {
		if (!template_node_->is_inside_tree()) memdelete(template_node_);
		template_node_ = nullptr;
	}
	model_path_ = "";
}

static int count_meshes(Node *n, int depth = 0) {
	if (!n || depth > 64) return 0;
	int c = 0;
	if (MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(n)) c += mi->get_mesh().is_valid() ? 1 : 0;
	for (int i = 0; i < n->get_child_count(); i++) c += count_meshes(n->get_child(i), depth + 1);
	return c;
}

bool JVModelLoader::load_template() {
	free_template();
	const md::Spec *s = spec();
	if (!s) { model_state_ = selected_ ? "error: equipped model pack is not usable" : "no model equipped"; return false; }
	const Pack &p = packs_[*selected_];
	String path = p.dir.path_join(G(s->model));
	model_path_ = path;
	auto err = [&](const String &why) {
		model_state_ = "error: " + why + " -> vanilla model";
		last_error_ = why;
		fallbacks_++;
		jvm::warn(kTag, "model " + G(sk::format_id(s->id)) + " ('" + p.folder + "'): " + why + " -> players keep the game's model");
		free_template();
		return false;
	};
	if (!FileAccess::file_exists(path)) return err("file not found: " + path);
	if (jvm::safe::pack_file(p.dir, G(s->model)).is_empty() || !jvm::safe::no_links(p.dir, path)) return err("model path " + G(s->model) + " is not a plain file inside the pack");
	if (s->ext == "glb") {
		String why;
		Node *n = jvm::safe::load_glb(path, why);
		if (!n) { jvm::safe::note_refusal("model " + path + ": " + why); return err("cannot read " + path + " (" + why + ")"); }
		template_node_ = n;
		if (count_meshes(n) == 0) return err("glTF " + path + " has no meshes");
	} else {
		return err("model " + path + " is ." + G(s->ext) + "; packs are data only, so models must be a self-contained .glb");
	}
	{
		Node *probe = template_scene_.is_valid() ? template_scene_->instantiate() : template_node_;
		std::vector<Node *> st{ probe };
		while (!st.empty() && probe && !have_template_own_) {
			Node *n = st.back(); st.pop_back();
			if (MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(n); mi && mi->get_mesh().is_valid()) {
				std::vector<Ref<Material>> ms{ mi->get_material_override() };
				for (int si = 0; si < mi->get_mesh()->get_surface_count(); si++) { Ref<Material> m = mi->get_surface_override_material(si); ms.push_back(m.is_valid() ? m : mi->get_mesh()->surface_get_material(si)); }
				for (auto &m : ms) {
					if (m.is_null() || have_template_own_) continue;
					if (Ref<ShaderMaterial> sm = m; sm.is_valid()) {
						Variant v = sm->get_shader_parameter(G(s->tint_param));
						if (v.get_type() != Variant::COLOR && sm->get_shader().is_valid()) v = RenderingServer::get_singleton()->shader_get_parameter_default(sm->get_shader()->get_rid(), G(s->tint_param));
						if (v.get_type() == Variant::COLOR) { template_own_ = v; have_template_own_ = true; }
					} else if (Ref<BaseMaterial3D> bm = m; bm.is_valid()) {
						bool ok = false;
						Color avg = tex_average(bm->get_texture(BaseMaterial3D::TEXTURE_ALBEDO), ok);
						template_own_ = ok ? bm->get_albedo() * avg : bm->get_albedo();
						have_template_own_ = true;
					}
				}
			}
			for (int i = n->get_child_count() - 1; i >= 0; i--) st.push_back(n->get_child(i));
		}
		if (template_scene_.is_valid() && probe) memdelete(probe);
		template_own_.a = 1;
	}
	model_state_ = "ready: " + G(sk::format_id(s->id)) + " " + G(s->name) + " (" + G(s->model) + ")";
	jvm::log(kTag, "loaded model " + G(sk::format_id(s->id)) + " '" + G(s->name) + "' from " + path);
	return true;
}


void JVModelLoader::notify_status_if_changed() {
	const int64_t applied = get_graft_count();
	String eq;
	if (selected_) eq = G(sk::format_id(*selected_));
	if (applied == last_status_applied_ && eq == last_status_equipped_) return;
	last_status_applied_ = applied;
	last_status_equipped_ = eq;
	if (is_inside_tree()) {
		if (Node *g = get_node_or_null(NodePath("/root/JVModGate"))) {
			if (g->has_method("mark_status_dirty")) g->call("mark_status_dirty");
		}
	}
}

void JVModelLoader::apply() {
	logged_.clear();
	load_packs();
	load_template();
	for (auto &r : rigs_) { r.failed = false; r.fail_reason = ""; r.eligible_t = 1e9; }
	scanned_ = false;
	_service();
	call_deferred("_service");
	notify_status_if_changed();
}

void JVModelLoader::revert() {
	int n = 0;
	for (auto &r : rigs_) if (r.grafted) { CtxGuard g(this, r); ungraft(r, true); r.remote.reset(); n++; }
	free_remote_templates();
	free_template();
	if (n) jvm::log(kTag, "reverted " + String::num_int64(n) + " model(s) to the game's model");
	notify_status_if_changed();
}

bool JVModelLoader::reload_config() {
	if (!follow_.applied) return false;
	revert();
	apply();
	return true;
}


void JVModelLoader::install_hook() {
	if (rig_script_.is_null()) {
		String path = kRigScript;
		OS *os = OS::get_singleton();
		if (os->is_debug_build() && !os->get_environment("JV_MODEL_RIG_SCRIPT").is_empty()) path = os->get_environment("JV_MODEL_RIG_SCRIPT");
		if (!ResourceLoader::get_singleton()->exists(path)) { hook_state_ = "ModelRig script not found (" + path + ")"; log_once("hook", hook_state_, true); return; }
		rig_script_ = jvm_models_io().resource("player.model", path);
		if (rig_script_.is_null()) { hook_state_ = "ModelRig script failed to load"; log_once("hook", hook_state_, true); return; }
	}
	if (!hook_.is_valid()) hook_ = Callable(this, "_on_model_hook");
	Variant cur = jvm_models_io().get("player.model", rig_script_, "model_hook");
	if (cur.get_type() == Variant::CALLABLE) {
		Callable c = cur;
		if (c == hook_) { hook_state_ = "installed"; return; }
		if (c.is_valid()) { chained_ = c; jvm::log(kTag, "ModelRig.model_hook already set (" + String(c.get_method()) + "); chaining it before ours"); }
	} else if (cur.get_type() != Variant::NIL) {
		hook_state_ = "ModelRig.model_hook not found (game changed?)"; log_once("hook", hook_state_, true); return;
	}
	jvm_models_io().set("player.model", rig_script_, "model_hook", hook_);
	Variant now = jvm_models_io().get("player.model", rig_script_, "model_hook");
	if (now.get_type() == Variant::CALLABLE && Callable(now) == hook_) {
		hook_state_ = "installed";
		jvm::log(kTag, "ModelRig.model_hook installed");
	} else {
		hook_state_ = "could not set ModelRig.model_hook";
		log_once("hook", hook_state_, true);
	}
}

void JVModelLoader::_on_model_hook(Object *scene_obj) {


	if (chained_.is_valid()) chained_.call(scene_obj);
	Node *scene = Object::cast_to<Node>(scene_obj);
	if (!scene) return;
	track(nullptr, scene);
	_service();
	call_deferred("_service");
}

void JVModelLoader::track(Node *rig, Node *scene) {
	if (!scene) return;
	for (auto &r : rigs_) if (r.scene == ObjectID(scene->get_instance_id())) { if (rig) r.rig = ObjectID(rig->get_instance_id()); return; }
	Rig r;
	r.scene = ObjectID(scene->get_instance_id());
	if (rig) r.rig = ObjectID(rig->get_instance_id());
	r.frame_added = int(Engine::get_singleton()->get_process_frames());
	rigs_.push_back(r);
}

void JVModelLoader::scan_tree(Node *n, int depth) {
	if (!n || depth > 96 || rig_script_.is_null()) return;
	Variant sc = n->get_script();
	if (sc.get_type() == Variant::OBJECT && Object::cast_to<Script>(sc.operator Object *()) == rig_script_.ptr()) {
		Node *scene = Object::cast_to<Node>(jvm_models_io().get("player.model", n, "_scene").operator Object *());
		if (scene) track(n, scene);
	}
	for (int i = 0; i < n->get_child_count(); i++) scan_tree(n->get_child(i), depth + 1);
}

Node *JVModelLoader::rig_of(const Rig &r) const { return inst<Node>(r.rig); }


static int32_t my_peer(Node *n) {
	Ref<MultiplayerAPI> mp = n->get_tree()->get_multiplayer();
	if (mp.is_null()) return 1;
	Ref<MultiplayerPeer> peer = mp->get_multiplayer_peer();
	if (peer.is_null() || peer->get_class() == "OfflineMultiplayerPeer") return 1;
	return mp->get_unique_id();
}






static bool presentation_is_me(Node *player) {
	return jvm::display_looks_like_me(jvm_models_io(), player);
}





bool JVModelLoader::is_reflection_rig(Node *rig) {
	if (!rig) return false;
	Variant rv = jvm_models_io().get("player.model", rig, "_reflection");
	if (rv.get_type() == Variant::BOOL && bool(rv)) return true;
	Node3D *holder = Object::cast_to<Node3D>(rig->get_parent());
	if (!holder || !holder->is_set_as_top_level()) return false;
	Node *player = holder->get_parent();
	if (!player || !jvm::script_is(player, "Player")) return false;
	Variant rr = jvm_models_io().get("player.model", player, "_reflect_root");
	return rr.get_type() == Variant::OBJECT && rr.operator Object *() == holder;
}

bool JVModelLoader::is_eligible(Node *rig) {
	const md::Spec *s = spec();
	if (!s) return false;
	if (is_reflection_rig(rig)) return false;
	if (s->target == md::Target::All) return true;
	Node *player = rig->get_parent();
	while (player && !jvm::script_is(player, "Player")) player = player->get_parent();
	if (!player) return true;
	if (is_locker_preview(player)) return true;
	if (jvm::is_character_portrait_player(player)) return presentation_is_me(player);

	Dictionary cfg;
	Variant sc = jvm::load_json(jvm::install_dir().path_join("texture_sync.json"));
	if (sc.get_type() == Variant::DICTIONARY) cfg = sc;
	OS *os = OS::get_singleton();
	if (os->is_debug_build() && !os->get_environment("JV_TEXTURE_SYNC").is_empty()) {
		Variant t = jvm::load_json(os->get_environment("JV_TEXTURE_SYNC"));
		if (t.get_type() == Variant::DICTIONARY) cfg = t;
	}
	StringName pp_name = String(cfg.get("players_property", "_peer_players"));
	StringName lp_name = String(cfg.get("local_player_property", "player"));
	const int32_t me = my_peer(rig);
	for (Node *a = player->get_parent(); a; a = a->get_parent()) {
		Variant lp = jvm_models_io().get("scene.players", a, lp_name);
		if (lp.get_type() == Variant::OBJECT && lp.operator Object *() == player)
			return jvm::script_is(a, "PresentationCharacter") ? presentation_is_me(player) : true;
		Variant pp = jvm_models_io().get("scene.players", a, pp_name);
		if (pp.get_type() == Variant::DICTIONARY) {
			Dictionary d = pp;
			if (d.has(me) && d[me].get_type() == Variant::OBJECT && d[me].operator Object *() == player) return true;
			Array vals = d.values();
			for (int i = 0; i < vals.size(); i++) if (vals[i].get_type() == Variant::OBJECT && vals[i].operator Object *() == player) return false;
		}
	}
	return false;
}

bool JVModelLoader::is_locker_preview(Node *player) {
	if (!player) return false;
	Node *locker = nullptr;
	for (Node *a = player->get_parent(); a; a = a->get_parent()) {
		if (jvm_is_locker(a)) { locker = a; break; }
	}
	if (!locker) {
		Node *cur = Object::cast_to<Node>(jvm_models_io().core("get_current_scene").operator Object *());
		if (cur && jvm_is_locker(cur)) locker = cur;
	}
	if (!locker) return false;
	if (bool(jvm_models_io().get("ui.locker", locker, "section_mode"))) return false;
	Variant pv = jvm_models_io().get("ui.locker", locker, "_player");
	Node *lp = (pv.get_type() == Variant::OBJECT) ? Object::cast_to<Node>(pv.operator Object *()) : nullptr;
	if (lp && lp == player) return true;
	for (Node *a = player; a; a = a->get_parent()) {
		if (a == lp || a == locker) return true;
	}
	return false;
}

static Node *find_locker_node(Node *root, int depth = 0) {
	if (!root || depth > 24) return nullptr;
	if (jvm_is_locker(root)) return root;
	for (int i = 0; i < root->get_child_count(); i++)
		if (Node *l = find_locker_node(root->get_child(i), depth + 1)) return l;
	return nullptr;
}
static void collect_lockers(Node *n, std::vector<ObjectID> &out, int depth = 0) {
	if (!n || depth > 64) return;
	if (jvm_is_locker(n)) out.push_back(ObjectID(n->get_instance_id()));
	for (int i = 0; i < n->get_child_count(); i++) collect_lockers(n->get_child(i), out, depth + 1);
}


static int depth_below(Node *n, Node *anc) {
	int d = 0;
	for (Node *a = n; a; a = a->get_parent(), d++) if (a == anc) return d;
	return -1;
}

void JVModelLoader::_on_node_added(Node *n) {
	if (lockers_seeded_ && jvm_is_locker(n)) lockers_.push_back(ObjectID(n->get_instance_id()));
}





Node *JVModelLoader::tracked_locker() {
	SceneTree *t = get_tree();
	if (!t) return nullptr;
	if (!lockers_seeded_) {
		lockers_seeded_ = true;
		if (!t->is_connected("node_added", callable_mp(this, &JVModelLoader::_on_node_added)))
			t->connect("node_added", callable_mp(this, &JVModelLoader::_on_node_added));
		collect_lockers(t->get_root(), lockers_);
	}
	Node *one = nullptr;
	int live = 0;
	for (size_t i = 0; i < lockers_.size();) {
		Node *n = Object::cast_to<Node>(ObjectDB::get_instance(lockers_[i]));
		if (!n) { lockers_.erase(lockers_.begin() + i); continue; }
		bool dup = false;
		for (size_t k = 0; k < i; k++) if (lockers_[k] == lockers_[i]) { dup = true; break; }
		if (dup) { lockers_.erase(lockers_.begin() + i); continue; }
		i++;
		if (!n->is_inside_tree() || !jvm_is_locker(n)) continue;
		live++;
		one = n;
	}
	if (live == 0) return nullptr;
	if (live > 1) {
		Node *l = find_locker_node(t->get_current_scene());
		return l ? l : find_locker_node(t->get_root());
	}
	Node *scene = t->get_current_scene();
	const int ds = scene ? depth_below(one, scene) : -1;
	if (ds >= 0 && ds <= 24) return one;
	const int dr = depth_below(one, t->get_root());
	return dr >= 0 && dr <= 24 ? one : nullptr;
}

static void update_locker_banner(Node *locker, const String &model_name) {
	if (!locker) return;
	String text;
	if (!model_name.is_empty()) text = "showing: " + model_name;
	else if (locker->has_method("_outfit_name")) text = jvm_models_io().call("ui.locker", locker, "_outfit_name");
	if (text.is_empty()) return;
	Variant b = jvm_models_io().get("ui.locker", locker, "_outfit_btn");
	if (Button *btn = Object::cast_to<Button>(b)) btn->set_text(text);
}


static Skeleton3D *find_skel(Node *n, int depth = 0) {
	if (!n || depth > 64) return nullptr;
	if (Skeleton3D *s = Object::cast_to<Skeleton3D>(n)) return s;
	for (int i = 0; i < n->get_child_count(); i++) if (Skeleton3D *s = find_skel(n->get_child(i), depth + 1)) return s;
	return nullptr;
}
static Transform3D xform_to(Node *root, Node *n) {
	Transform3D t;
	for (Node *c = n; c && c != root; c = c->get_parent())
		if (Node3D *c3 = Object::cast_to<Node3D>(c)) t = c3->get_transform() * t;
	return t;
}
static void clear_owner(Node *n) {
	n->set_owner(nullptr);
	for (int i = 0; i < n->get_child_count(); i++) clear_owner(n->get_child(i));
}
static void mark(Node *n, std::set<uint64_t> &ids, uint32_t layers, bool set_layers) {
	n->set_meta(kGraftMeta, true);
	ids.insert(uint64_t(n->get_instance_id()));
	if (set_layers) if (VisualInstance3D *v = Object::cast_to<VisualInstance3D>(n)) v->set_layer_mask(layers);
	for (int i = 0; i < n->get_child_count(); i++) mark(n->get_child(i), ids, layers, set_layers);
}

void JVModelLoader::fail(Rig &r, const String &why) {
	r.failed = true;
	r.fail_reason = why;
	last_error_ = why;
	fallbacks_++;
	Node *rig = rig_of(r);
	log_once("fail", "model not applied to " + (rig ? String(rig->get_path()) : String("?")) + ": " + why + " -> game's model kept", true);
}

bool JVModelLoader::graft(Rig &r) {
	const md::Spec *s = spec();
	Node *rig = rig_of(r);
	Node *scene = inst<Node>(r.scene);
	if (!s || !rig || !scene) return false;
	Skeleton3D *base = find_skel(scene);
	if (!base) { fail(r, "the game's model has no Skeleton3D"); return false; }
	Node *src = nullptr;
	if (template_scene_.is_valid()) src = template_scene_->instantiate();
	else if (template_node_) src = template_node_->duplicate();
	if (!src) { fail(r, "model could not be instantiated"); return false; }
	Skeleton3D *cs = find_skel(src);

	struct Unit { Node *node; int kind; Transform3D xf; String bone; Ref<Skin> skin; };
	enum { SKINNED, ATTACH, STATIC };
	std::vector<Unit> units;
	std::vector<std::string> needed;
	Node *space = cs ? static_cast<Node *>(cs) : src;
	Transform3D space_inv = xform_to(src, space).affine_inverse();
	std::vector<Node *> stack{ src };
	while (!stack.empty()) {
		Node *n = stack.back(); stack.pop_back();
		bool unit = false;
		if (n != src) {
			if (BoneAttachment3D *ba = Object::cast_to<BoneAttachment3D>(n)) {
				String bone = ba->get_bone_name();
				if (bone.is_empty() && cs && ba->get_bone_idx() >= 0 && ba->get_bone_idx() < cs->get_bone_count()) bone = cs->get_bone_name(ba->get_bone_idx());
				units.push_back({ n, ATTACH, Transform3D(), bone, Ref<Skin>() });
				needed.push_back(S(bone));
				unit = true;
			} else if (MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(n)) {
				if (mi->get_mesh().is_valid()) {
					Transform3D xf = space_inv * xform_to(src, n);
					Ref<Skin> skin = mi->get_skin();
					if (skin.is_valid() && cs) {
						Ref<Skin> ns; ns.instantiate();
						for (int b = 0; b < skin->get_bind_count(); b++) {
							String name = skin->get_bind_name(b);
							if (name.is_empty()) {
								int bone = skin->get_bind_bone(b);
								name = (bone >= 0 && bone < cs->get_bone_count()) ? cs->get_bone_name(bone) : String();
							}
							needed.push_back(S(name));
							ns->add_named_bind(name, skin->get_bind_pose(b));
						}
						units.push_back({ n, SKINNED, xf, String(), ns });
					} else if (skin.is_valid() && !cs) {
						memdelete(src);
						fail(r, "skinned mesh '" + String(n->get_name()) + "' but the model has no Skeleton3D");
						return false;
					} else {
						units.push_back({ n, STATIC, xf, String(), Ref<Skin>() });
					}
					unit = true;
				}
			}
		}
		if (!unit) for (int i = n->get_child_count() - 1; i >= 0; i--) stack.push_back(n->get_child(i));
	}
	if (units.empty()) { memdelete(src); fail(r, "model has no meshes"); return false; }
	std::vector<std::string> base_names;
	for (int i = 0; i < base->get_bone_count(); i++) base_names.push_back(S(base->get_bone_name(i)));
	md::BoneCheck bc = md::check_bones(needed, base_names);
	if (!bc.ok) {
		memdelete(src);
		fail(r, "skeleton mismatch: the game's Armature/Skeleton3D has no bone(s) " + G(md::join(bc.missing, ", ")));
		return false;
	}

	for (auto &u : units) {
		if (u.kind == ATTACH) u.bone = G(bc.resolved[S(u.bone)]);
		else if (u.kind == SKINNED) {
			Ref<Skin> fixed; fixed.instantiate();
			for (int b = 0; b < u.skin->get_bind_count(); b++) fixed->add_named_bind(G(bc.resolved[S(String(u.skin->get_bind_name(b)))]), u.skin->get_bind_pose(b));
			u.skin = fixed;
		}
	}

	double rest_err = 0.0;
	if (cs) for (int i = 0; i < cs->get_bone_count(); i++) {
		auto it = bc.resolved.find(S(cs->get_bone_name(i)));
		int j = it != bc.resolved.end() ? base->find_bone(G(it->second)) : base->find_bone(cs->get_bone_name(i));
		if (j < 0) continue;
		rest_err = std::max(rest_err, double((cs->get_bone_global_rest(i).origin - base->get_bone_global_rest(j).origin).length()));
	}
	r.rest_error = rest_err;
	if (rest_err > s->max_rest_error)
		log_once("rest", "model rest pose differs from the game's by up to " + String::num(rest_err, 3) + " rig units (max_rest_error " + String::num(s->max_rest_error, 3) + "); it may look stretched", true);


	uint32_t layers = 1;
	bool have_layers = false;
	{
		std::vector<Node *> st{ scene };
		while (!st.empty() && !have_layers) {
			Node *n = st.back(); st.pop_back();
			if (GeometryInstance3D *g = Object::cast_to<GeometryInstance3D>(n)) if (!n->has_meta(kGraftMeta)) { layers = g->get_layer_mask(); have_layers = true; break; }
			for (int i = 0; i < n->get_child_count(); i++) st.push_back(n->get_child(i));
		}
	}
	r.grafts.clear(); r.graft_set.clear(); r.mats.clear();
	int idx = 0;
	for (auto &u : units) {
		Node *parent = u.node->get_parent();
		if (parent) parent->remove_child(u.node);
		clear_owner(u.node);
		u.node->set_meta("jvm_model_source_name", u.node->get_name());
		u.node->set_name("JvmModel" + String::num_int64(idx++));
		if (Node3D *n3 = Object::cast_to<Node3D>(u.node); n3 && u.kind != ATTACH) n3->set_transform(u.xf);
		base->add_child(u.node);
		if (u.kind == SKINNED) {
			MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(u.node);
			mi->set_skin(u.skin);
			mi->set_skeleton_path(NodePath(".."));
		} else if (u.kind == ATTACH) {
			BoneAttachment3D *ba = Object::cast_to<BoneAttachment3D>(u.node);
			ba->set_use_external_skeleton(false);
			ba->set_bone_name(u.bone);
		}
		mark(u.node, r.graft_set, layers, have_layers);
		r.grafts.push_back(ObjectID(u.node->get_instance_id()));
	}
	memdelete(src);


	auto take = [&](const Ref<Material> &m) -> Ref<Material> {
		if (m.is_null()) return m;
		Ref<Material> d = m->duplicate();
		TintMat t;
		t.mat = d;
		if (Ref<ShaderMaterial> sm = d; sm.is_valid()) {
			t.shader = true;
			Ref<Shader> sh = sm->get_shader();
			std::set<std::string> names;
			if (sh.is_valid()) {
				Array u = sh->get_shader_uniform_list();
				for (int i = 0; i < u.size(); i++) names.insert(S(String(Dictionary(u[i]).get("name", ""))));
			}
			t.has_param = names.count(s->tint_param) > 0;
			if (t.has_param) {
				Variant v = sm->get_shader_parameter(G(s->tint_param));
				t.param_orig = v;
				if (v.get_type() != Variant::COLOR && sh.is_valid()) v = RenderingServer::get_singleton()->shader_get_parameter_default(sh->get_rid(), G(s->tint_param));
				t.original = v.get_type() == Variant::COLOR ? Color(v) : Color(1, 1, 1, 1);
				t.own = t.original;
			}
			for (auto &[k, amt] : s->tint_light) if (names.count(k)) { t.light.push_back({ StringName(G(k)), amt }); t.light_orig.push_back(sm->get_shader_parameter(G(k))); }
			t.has_flash = names.count("flash") && names.count("flash_color");
		} else if (Ref<BaseMaterial3D> bm = d; bm.is_valid()) {
			t.has_param = true;
			t.factor_orig = bm->get_albedo();
			t.tex_orig = bm->get_texture(BaseMaterial3D::TEXTURE_ALBEDO);
			bool ok = false;
			Color avg = tex_average(t.tex_orig, ok);
			t.original = t.own = ok ? t.factor_orig * avg : t.factor_orig;
			t.own.a = t.original.a = t.factor_orig.a;
		}
		r.mats.push_back(t);
		return d;
	};
	for (uint64_t id : r.graft_set) {
		MeshInstance3D *mi = inst<MeshInstance3D>(ObjectID(id));
		if (!mi || mi->get_mesh().is_null()) continue;
		if (mi->get_material_override().is_valid()) { mi->set_material_override(take(mi->get_material_override())); continue; }
		Ref<Mesh> mesh = mi->get_mesh();
		for (int si = 0; si < mesh->get_surface_count(); si++) {
			Ref<Material> m = mi->get_surface_override_material(si);
			if (m.is_null()) m = mesh->surface_get_material(si);
			if (m.is_valid()) mi->set_surface_override_material(si, take(m));
		}
	}
	paint(r, false);
	build_arms(r, base, layers, have_layers);

	if (Node *vis = rig->get_parent()) {
		r.hands[0] = jvm_models_io().node("player.model", vis, "HandL") ? ObjectID(jvm_models_io().node("player.model", vis, "HandL")->get_instance_id()) : ObjectID();
		r.hands[1] = jvm_models_io().node("player.model", vis, "HandR") ? ObjectID(jvm_models_io().node("player.model", vis, "HandR")->get_instance_id()) : ObjectID();
	}
	r.grafted = true;
	r.last_tint = Color(-1, -1, -1, -1);
	r.last_flash = -1.0f;
	grafts_done_++;
	if (Node *rp = rig->get_parent()) {
		Node *player = rp;
		while (player && !jvm::script_is(player, "Player")) player = player->get_parent();
		if (is_locker_preview(player ? player : rp)) {


			for (ObjectID gid : r.grafts) {
				if (VisualInstance3D *vi = inst<VisualInstance3D>(gid)) {
					vi->set_visible(true);
					vi->set_layer_mask(vi->get_layer_mask() | 1u);
				}
			}
		}
	}
	jvm::log(kTag, "model " + G(sk::format_id(s->id)) + " on " + String(rig->get_path()) + ": " + String::num_int64(int64_t(units.size())) +
			" part(s) bound to the game's skeleton (" + String::num_int64(base->get_bone_count()) + " bones, rest diff " + String::num(rest_err, 4) + ")");
	sweep(r);
	update_tint(r);
	return true;
}

void JVModelLoader::hook_stock_visibility(Rig &r, Node *n, uint64_t id) {
	if (!n || r.hidden_hooked.count(id)) return;
	Callable cb = Callable(this, "_on_stock_visibility_changed").bind(int64_t(id));
	if (!n->is_connected("visibility_changed", cb)) {
		Error err = n->connect("visibility_changed", cb);
		if (err != OK) {
			jvm::log(kTag, "could not watch stock visibility on " + String(n->get_path()) + " err=" + String::num_int64(int64_t(err)));
			return;
		}
	}
	r.hidden_hooked.insert(id);
}

void JVModelLoader::unhook_stock_visibility(Rig &r) {
	Callable base = Callable(this, "_on_stock_visibility_changed");
	for (uint64_t id : r.hidden_hooked) {
		if (Node *n = inst<Node>(ObjectID(id))) {
			Callable cb = base.bind(int64_t(id));
			if (n->is_connected("visibility_changed", cb)) n->disconnect("visibility_changed", cb);
		}
	}
	r.hidden_hooked.clear();
}

void JVModelLoader::_on_stock_visibility_changed(int64_t id64) {





	if (rehide_guard_) return;
	uint64_t id = uint64_t(id64);
	Node3D *n = inst<Node3D>(ObjectID(id));
	if (!n || !n->is_visible()) return;


	String nm = String(n->get_name());
	if (n->has_meta("jvm_accessory") || n->has_meta("jvm_acc_id") ||
			nm.begins_with("JvmAcc_") || nm.begins_with("JVM_"))
		return;
	if (n->has_meta(kGraftMeta)) return;
	for (auto &r : rigs_) {
		if (!r.grafted || !r.hidden.count(id) || r.graft_set.count(id)) continue;
		rehide_guard_ = true;
		n->set_visible(false);
		if (VisualInstance3D *vi = Object::cast_to<VisualInstance3D>(n)) {

			if (!r.hidden_layers.count(id)) r.hidden_layers[id] = vi->get_layer_mask();
			vi->set_layer_mask(0);
		}
		rehide_guard_ = false;
		return;
	}
}

void JVModelLoader::ungraft(Rig &r, bool restore) {
	unhook_stock_visibility(r);
	for (ObjectID id : r.grafts) {
		if (Node *n = inst<Node>(id)) {
			if (Node *p = n->get_parent()) p->remove_child(n);



			memdelete(n);
		}
	}
	if (restore) {
		for (uint64_t id : r.hidden) {
			if (Node3D *n = inst<Node3D>(ObjectID(id))) {
				auto vit = r.hidden_was_visible.find(id);



				n->set_visible(vit != r.hidden_was_visible.end() ? vit->second : true);
			}
			auto lit = r.hidden_layers.find(id);
			if (lit != r.hidden_layers.end())
				if (VisualInstance3D *vi = inst<VisualInstance3D>(ObjectID(id))) vi->set_layer_mask(lit->second);
		}
		Node *rig = rig_of(r);
		for (int i = 0; i < 2; i++) {
			MeshInstance3D *h = inst<MeshInstance3D>(r.hands[i]);
			if (!h) continue;
			if (r.hands_hidden[i]) h->set_visible(true);
			const md::Spec *s = spec();
			if (s && s->hands == md::Hands::Tint && rig && rig->has_method("skin_color")) {
				Ref<ShaderMaterial> m = h->get_material_override();
				if (m.is_valid()) m->set_shader_parameter("albedo", jvm_models_io().call("player.model", rig, "skin_color"));
			}
		}
	}
	r.grafts.clear(); r.graft_set.clear(); r.hidden.clear(); r.hidden_layers.clear(); r.hidden_was_visible.clear(); r.hidden_hooked.clear(); r.mats.clear();
	r.hands_hidden[0] = r.hands_hidden[1] = false;
	r.grafted = false;
}


void JVModelLoader::sweep(Rig &r) {
	const md::Spec *s = spec();
	Node *rig = rig_of(r);
	if (!s || !rig || !s->hide_base) return;



	std::unordered_map<Node *, Node *> nearest;
	auto player_of = [&](Node *from) -> Node * {
		std::vector<Node *> path;
		Node *p = from;
		Node *found = nullptr;
		for (; p; p = p->get_parent()) {
			auto it = nearest.find(p);
			if (it != nearest.end()) { found = it->second; break; }
			path.push_back(p);
			if (jvm::script_is(p, "Player")) { found = p; break; }
		}
		for (Node *q : path) nearest[q] = found;
		return found;
	};
	std::unordered_map<Node *, bool> locker_of;
	auto locker_preview = [&](Node *player) -> bool {
		if (!player) return false;
		auto it = locker_of.find(player);
		if (it != locker_of.end()) return it->second;
		return locker_of[player] = is_locker_preview(player);
	};
	static const StringName kAccMeta("jvm_accessory"), kAccIdMeta("jvm_acc_id");
	std::vector<Node *> st{ rig };



	Node *player = rig->get_parent() ? player_of(rig->get_parent()) : nullptr;
	if (player) {
		if (Node *visual = jvm_models_io().node("player.model", player, "Visual")) st.push_back(visual);
		Variant visv = jvm_models_io().get("player.model", player, "visual");
		if (visv.get_type() == Variant::OBJECT)
			if (Node *v = Object::cast_to<Node>(visv.operator Object *())) st.push_back(v);
		if (Node *head = jvm_models_io().node("player.model", player, "Head")) st.push_back(head);
		if (Node *body = jvm_models_io().node("player.model", player, "Body")) st.push_back(body);


		if (locker_preview(player)) st.push_back(player);
	}
	std::unordered_set<uint64_t> queued;
	while (!st.empty()) {
		Node *n = st.back(); st.pop_back();
		uint64_t id = uint64_t(n->get_instance_id());
		if (!queued.insert(id).second) continue;
		if (r.graft_set.count(id)) continue;


		if (n != rig) {

			if (n->has_meta(kGraftMeta)) continue;

			if (rig_script_.is_valid()) {
				Variant sc = n->get_script();
				if (sc.get_type() == Variant::OBJECT && Object::cast_to<Script>(sc.operator Object *()) == rig_script_.ptr()) continue;
			}
			if (n->has_meta(kAccMeta) || n->has_meta(kAccIdMeta)) continue;
			const String nm = n->get_name();
			if (nm.begins_with("JVM_") || nm.begins_with("JvmAcc_")) continue;
		}
		if (Object::cast_to<GeometryInstance3D>(n) || Object::cast_to<Decal>(n)) {
			Node3D *v = Object::cast_to<Node3D>(n);
			const bool locker = locker_preview(player_of(v));
			if (v->is_visible() || locker) {
				if (!r.hidden_was_visible.count(id))
					r.hidden_was_visible[id] = v->is_visible();
				v->set_visible(false);
				r.hidden.insert(id);
				hook_stock_visibility(r, v, id);
				if (locker) {
					if (VisualInstance3D *vi = Object::cast_to<VisualInstance3D>(v)) {
						if (!r.hidden_layers.count(id)) r.hidden_layers[id] = vi->get_layer_mask();
						vi->set_layer_mask(0);
					}
				}
			}
		}
		for (int i = 0; i < n->get_child_count(); i++) st.push_back(n->get_child(i));
	}
}



void JVModelLoader::paint(Rig &r, bool restore) {
	const md::Spec *s = spec();
	if (!s) return;
	Color c(s->color[0], s->color[1], s->color[2], s->color[3]);
	for (auto &t : r.mats) {
		if (!t.has_param) continue;
		if (t.shader) {
			Ref<ShaderMaterial> sm = t.mat;
			if (s->has_color) {
				sm->set_shader_parameter(G(s->tint_param), c);
				for (auto &[k, a] : t.light) { Color l = c.lerp(Color(1, 1, 1, 1), a); l.a = 1; sm->set_shader_parameter(k, l); }
			} else if (restore) {
				sm->set_shader_parameter(G(s->tint_param), t.param_orig);
				for (size_t i = 0; i < t.light.size() && i < t.light_orig.size(); i++) sm->set_shader_parameter(t.light[i].first, t.light_orig[i]);
			}
		} else {
			Ref<BaseMaterial3D> bm = t.mat;
			if (s->has_color) {
				bm->set_albedo(c);

				Ref<Texture2D> g = s->user_color ? grey_of(t.tex_orig) : Ref<Texture2D>();
				if (g.is_valid()) bm->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, g);
				else if (restore) bm->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, t.tex_orig);
			} else if (restore) {
				bm->set_albedo(t.factor_orig);
				bm->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, t.tex_orig);
			}
		}
		t.original = s->has_color ? c : t.own;
	}
	r.last_tint = Color(-1, -1, -1, -1);
}

Color JVModelLoader::tex_average(const Ref<Texture2D> &tex, bool &ok) {
	ok = false;
	if (tex.is_null()) return Color(1, 1, 1, 1);
	uint64_t k = uint64_t(tex->get_instance_id());
	if (auto it = avg_cache_.find(k); it != avg_cache_.end()) { ok = it->second.a >= 0; return ok ? it->second : Color(1, 1, 1, 1); }
	Ref<Image> img = tex->get_image();
	if (img.is_null() || img->is_empty()) { avg_cache_[k] = Color(0, 0, 0, -1); return Color(1, 1, 1, 1); }
	img = img->duplicate();
	if (img->is_compressed()) img->decompress();
	img->clear_mipmaps();
	img->convert(Image::FORMAT_RGBA8);
	PackedByteArray d = img->get_data();
	double sum[3] = { 0, 0, 0 };
	int64_t n = d.size() / 4;
	const uint8_t *px = d.ptr();
	for (int64_t i = 0; i < n; i++) for (int c = 0; c < 3; c++) sum[c] += px[i * 4 + c];
	if (n == 0) { avg_cache_[k] = Color(0, 0, 0, -1); return Color(1, 1, 1, 1); }
	Color a(float(sum[0] / n / 255.0), float(sum[1] / n / 255.0), float(sum[2] / n / 255.0), 1);
	avg_cache_[k] = a;
	ok = true;
	return a;
}

Ref<Texture2D> JVModelLoader::grey_of(const Ref<Texture2D> &tex) {
	if (tex.is_null()) return tex;
	uint64_t k = uint64_t(tex->get_instance_id());
	if (auto it = grey_cache_.find(k); it != grey_cache_.end()) return it->second;
	Ref<Texture2D> out;
	Ref<Image> img = tex->get_image();
	if (img.is_valid() && !img->is_empty()) {
		img = img->duplicate();
		if (img->is_compressed()) img->decompress();
		img->clear_mipmaps();
		img->convert(Image::FORMAT_RGBA8);
		PackedByteArray d = img->get_data();
		int64_t n = d.size() / 4;
		uint8_t *px = d.ptrw();
		double sum = 0;
		std::vector<float> lum(size_t(std::max<int64_t>(n, 0)));
		for (int64_t i = 0; i < n; i++) { lum[i] = 0.2126f * px[i * 4] + 0.7152f * px[i * 4 + 1] + 0.0722f * px[i * 4 + 2]; sum += lum[i]; }
		double avg = n ? sum / n : 0;
		if (avg > 0.5) {
			for (int64_t i = 0; i < n; i++) {
				uint8_t g = uint8_t(std::min(255.0, std::round(255.0 * lum[i] / avg)));
				px[i * 4] = px[i * 4 + 1] = px[i * 4 + 2] = g;
			}
			Ref<Image> gi = Image::create_from_data(img->get_width(), img->get_height(), false, Image::FORMAT_RGBA8, d);
			if (gi->generate_mipmaps() != OK)
				jvm::warn("models", "grey texture generate_mipmaps failed");
			out = ImageTexture::create_from_image(gi);
		}
	}
	grey_cache_[k] = out;
	return out;
}





void JVModelLoader::build_arms(Rig &r, Skeleton3D *base, uint32_t layers, bool have_layers) {
	r.arms_on = false;
	const md::Spec *s = spec();
	if (!s || !s->arms.follow || !base) return;
	const md::Arms &a = s->arms;
	Ref<Material> mat;
	if (!r.mats.empty()) mat = r.mats[0].mat;
	r.arm_anchor = base->find_bone(G(a.anchor_bone));
	if (r.arm_anchor < 0) log_once("arms_anchor", "arms: anchor bone '" + G(a.anchor_bone) + "' not on the game skeleton; shoulders fixed in skeleton space", true);

	Node *rig = rig_of(r);
	Variant bv = rig ? jvm_models_io().get("player.model", rig, "_bone") : Variant();
	if (bv.get_type() == Variant::DICTIONARY) {
		Dictionary bd = bv;
		const char *keys[2] = { "l", "r" };
		for (int i = 0; i < 2; i++) {
			int b = bd.has(keys[i]) ? int(bd[keys[i]]) : -1;
			if (b >= 0 && b < base->get_bone_count()) {
				double x = base->get_bone_global_rest(b).origin.x;
				if (std::fabs(x) > 1e-4) r.arm_side[i] = x < 0 ? -1 : 1;
			}
		}
	}
	auto add = [&](Ref<Mesh> m, const String &name) -> ObjectID {
		MeshInstance3D *mi = memnew(MeshInstance3D);
		mi->set_name(name);
		mi->set_mesh(m);
		if (mat.is_valid()) mi->set_material_override(mat);
		mi->set_as_top_level(true);
		mi->set_physics_interpolation_mode(Node::PHYSICS_INTERPOLATION_MODE_OFF);
		mi->set_visible(false);
		base->add_child(mi);
		mark(mi, r.graft_set, layers, have_layers);
		r.grafts.push_back(ObjectID(mi->get_instance_id()));
		return ObjectID(mi->get_instance_id());
	};
	auto cyl = [&](double r0, double r1) {
		Ref<CylinderMesh> c; c.instantiate();
		c->set_top_radius(float(r1)); c->set_bottom_radius(float(r0)); c->set_height(1.0f);
		c->set_radial_segments(20); c->set_rings(1); c->set_cap_top(false); c->set_cap_bottom(false);
		return Ref<Mesh>(c);
	};
	auto sph = [&](double rad) {
		Ref<SphereMesh> m; m.instantiate();
		m->set_radius(float(rad)); m->set_height(float(rad * 2)); m->set_radial_segments(20); m->set_rings(10);
		return Ref<Mesh>(m);
	};
	for (int i = 0; i < 2; i++) {
		r.arm[i][0] = add(cyl(a.radius[0], a.radius[1]), String("JvmArmUpper") + (i ? "R" : "L"));
		r.arm[i][1] = add(cyl(a.radius[1], a.radius[2]), String("JvmArmLower") + (i ? "R" : "L"));
		r.arm[i][2] = add(sph(a.radius[0]), String("JvmArmShoulder") + (i ? "R" : "L"));
		r.arm[i][3] = add(sph(a.radius[1]), String("JvmArmElbow") + (i ? "R" : "L"));
		r.arm[i][4] = add(sph(a.radius[2]), String("JvmArmWrist") + (i ? "R" : "L"));
	}
	r.arms_on = true;
}

void JVModelLoader::update_arms(Rig &r) {
	if (!r.arms_on) return;
	const md::Spec *s = spec();
	Node *scene = inst<Node>(r.scene);
	Skeleton3D *base = scene ? find_skel(scene) : nullptr;
	if (!s || !base || !base->is_inside_tree()) return;
	const md::Arms &a = s->arms;
	Transform3D sk = base->get_global_transform_interpolated();
	Transform3D anchor;
	if (r.arm_anchor >= 0) anchor = base->get_bone_global_pose(r.arm_anchor) * base->get_bone_global_rest(r.arm_anchor).affine_inverse();
	Node *rig = rig_of(r);
	Node3D *rig3 = Object::cast_to<Node3D>(rig);



	int hands_ok = -1;
	auto hands_animated = [&]() -> bool {
		if (hands_ok < 0) hands_ok = rig && rig->has_method("hands_animated") && bool(jvm_models_io().call("player.model", rig, "hands_animated")) ? 1 : 0;
		return hands_ok == 1;
	};
	for (int i = 0; i < 2; i++) {
		double sx = r.arm_side[i];
		Vector3 sh_local = anchor.xform(Vector3(float(a.shoulder[0] * sx), float(a.shoulder[1]), float(a.shoulder[2])));
		Vector3 sh_w = sk.xform(sh_local);
		Vector3 hand_w;
		bool have = false;
		if (Node3D *h = inst<Node3D>(r.hands[i]); h && h->is_inside_tree()) { hand_w = h->get_global_transform_interpolated().origin; have = true; }
		else if (rig3 && rig3->is_inside_tree() && rig3->has_method("hand_local") && hands_animated()) {
			Variant hv = jvm_models_io().call("player.model", rig3, "hand_local", i == 1);
			if (hv.get_type() == Variant::VECTOR3) { hand_w = rig3->get_global_transform_interpolated().xform(Vector3(hv)); have = true; }
		}
		if (!have) { for (ObjectID id : r.arm[i]) if (Node3D *n = inst<Node3D>(id)) n->set_visible(false); continue; }

		Transform3D inv = sk.affine_inverse();
		Vector3 hand_l = inv.xform(hand_w);
		Vector3 hint = anchor.basis.xform(Vector3(float(a.elbow_hint[0] * sx), float(a.elbow_hint[1]), float(a.elbow_hint[2])));
		md::V3 e = md::solve_elbow({ sh_local.x, sh_local.y, sh_local.z }, { hand_l.x, hand_l.y, hand_l.z }, a.upper, a.lower, { hint.x, hint.y, hint.z });
		Vector3 el_w = sk.xform(Vector3(float(e.x), float(e.y), float(e.z)));

		Vector3 wr_w = hand_w;
		r.arm_gap[i] = std::max(0.0, double(sh_local.distance_to(hand_l)) - (a.upper + a.lower));
		auto seg = [&](ObjectID id, const Vector3 &from, const Vector3 &to) {
			Node3D *n = inst<Node3D>(id);
			if (!n) return;
			Vector3 d = to - from;
			float len = d.length();
			if (len < 1e-5f) { n->set_visible(false); return; }
			Vector3 y = d / len;
			Vector3 ref = std::fabs(y.dot(Vector3(0, 0, 1))) < 0.9f ? Vector3(0, 0, 1) : Vector3(1, 0, 0);
			Vector3 x = ref.cross(y).normalized();
			Vector3 z = x.cross(y).normalized();
			float sc = sk.basis.get_scale().x;
			Basis b;
			b.set_column(0, x * sc); b.set_column(1, y * len); b.set_column(2, z * sc);
			n->set_global_transform(Transform3D(b, (from + to) * 0.5f));
			n->set_visible(true);
		};
		auto ball = [&](ObjectID id, const Vector3 &at) {
			Node3D *n = inst<Node3D>(id);
			if (!n) return;
			float sc = sk.basis.get_scale().x;
			n->set_global_transform(Transform3D(Basis().scaled(Vector3(sc, sc, sc)), at));
			n->set_visible(true);
		};
		seg(r.arm[i][0], sh_w, el_w);
		seg(r.arm[i][1], el_w, wr_w);
		ball(r.arm[i][2], sh_w);
		ball(r.arm[i][3], el_w);
		ball(r.arm[i][4], wr_w);
	}
}

void JVModelLoader::update_hands(Rig &r, const Color &c, bool have_color) {
	const md::Spec *s = spec();
	if (!s || s->hands == md::Hands::Keep) return;
	for (int i = 0; i < 2; i++) {
		MeshInstance3D *h = inst<MeshInstance3D>(r.hands[i]);
		if (!h) continue;
		if (s->hands == md::Hands::Hide) {
			if (h->is_visible()) { h->set_visible(false); r.hands_hidden[i] = true; }
		} else if (have_color) {
			Ref<ShaderMaterial> m = h->get_material_override();
			if (m.is_valid()) {
				Variant cur = m->get_shader_parameter("albedo");
				if (cur.get_type() != Variant::COLOR || Color(cur) != c) m->set_shader_parameter("albedo", c);
			}
		}
	}
}

void JVModelLoader::update_tint(Rig &r) {
	const md::Spec *s = spec();
	Node *rig = rig_of(r);
	if (!s || !rig) return;
	Variant mv = jvm_models_io().get("player.model", rig, "_mats");
	Dictionary mats = mv.get_type() == Variant::DICTIONARY ? Dictionary(mv) : Dictionary();
	Color src;
	bool have = false;
	if (s->tint != md::Tint::None) {
		Variant m = mats.get(s->tint == md::Tint::Jersey ? "jersey" : "skin", Variant());
		if (m.get_type() != Variant::OBJECT && s->tint == md::Tint::Jersey) m = jvm_models_io().get("player.model", rig, "_fallback_mat");
		if (Ref<ShaderMaterial> sm = m; sm.is_valid()) {
			Variant v = sm->get_shader_parameter("albedo");
			if (v.get_type() == Variant::COLOR) { src = v; have = true; }
		}
	}
	Color hand_c;
	bool hand_have = false;
	if (have) {
		if (src != r.last_tint) {
			for (auto &t : r.mats) {
				if (!t.has_param) continue;
				Color c = t.original.lerp(src, float(s->tint_strength));
				c.a = t.original.a;
				if (t.shader) {
					Ref<ShaderMaterial> sm = t.mat;
					sm->set_shader_parameter(G(s->tint_param), c);
					for (auto &[k, a] : t.light) { Color l = c.lerp(Color(1, 1, 1, 1), float(a)); l.a = 1; sm->set_shader_parameter(k, l); }
				} else { Ref<BaseMaterial3D> bm = t.mat; bm->set_albedo(c); }
			}
			r.last_tint = src;
		}
		hand_c = r.mats.empty() ? src : r.mats[0].original.lerp(src, float(s->tint_strength));
		hand_have = true;
	} else if (!r.mats.empty() && r.mats[0].has_param) {
		hand_c = r.mats[0].original;
		hand_have = true;
	}
	if (hand_have) hand_c.a = 1;
	if (s->flash) {
		Variant m = mats.get("skin", Variant());
		if (m.get_type() != Variant::OBJECT) m = mats.get("jersey", Variant());
		if (Ref<ShaderMaterial> sm = m; sm.is_valid()) {
			Variant f = sm->get_shader_parameter("flash");
			float fv = (f.get_type() == Variant::FLOAT || f.get_type() == Variant::INT) ? float(f) : 0.0f;
			if (fv != r.last_flash) {
				Variant fc = sm->get_shader_parameter("flash_color");
				for (auto &t : r.mats) if (t.has_flash) {
					Ref<ShaderMaterial> tm = t.mat;
					tm->set_shader_parameter("flash", fv);
					if (fc.get_type() == Variant::COLOR) tm->set_shader_parameter("flash_color", fc);
				}
				r.last_flash = fv;
			}
		}
	}
	update_hands(r, hand_c, hand_have);
}


void JVModelLoader::_service() {
	if (!follow_.applied) return;
	if (!scanned_ && rig_script_.is_valid() && is_inside_tree()) { scanned_ = true; scan_tree(get_tree()->get_root(), 0); }
	const bool ready = spec() && (template_scene_.is_valid() || template_node_);
	for (size_t i = 0; i < rigs_.size();) {
		Rig &r = rigs_[i];
		Node *scene = inst<Node>(r.scene);
		if (!scene) { rigs_.erase(rigs_.begin() + i); continue; }
		if (!r.rig.is_valid()) {
			if (Node *p = scene->get_parent()) r.rig = ObjectID(p->get_instance_id());
		}
		Node *rig = rig_of(r);
		if (!rig || !scene->is_inside_tree()) { i++; continue; }
		Node *elig_player = rig->get_parent();
		while (elig_player && !jvm::script_is(elig_player, "Player")) elig_player = elig_player->get_parent();
		const bool portrait = jvm::is_character_portrait_player(elig_player);
		if (r.eligible_t >= 0.5 || portrait) {
			r.reflection = is_reflection_rig(rig);
			r.eligible = !r.reflection && is_eligible(rig);
			r.remote_want = (ready && r.eligible) || r.reflection || remote_models_.empty() ? std::nullopt : remote_for(rig);
			r.eligible_t = portrait ? 0.5 : 0;
		}
		const bool local = ready && r.eligible && !r.failed;

		std::optional<sk::SkinId> rw = local || r.failed ? std::nullopt : r.remote_want;
		if (rw && !ensure_remote_template(*rw)) rw.reset();
		const bool want = local || rw.has_value();
		if (r.grafted && (!want || r.remote != rw)) { { CtxGuard g(this, r); ungraft(r, true); } r.remote.reset(); }
		if (want && !r.grafted) { r.remote = rw; CtxGuard g(this, r); graft(r); }
		i++;
	}
	Node *locker = tracked_locker();
	if (locker) {
		String name;
		if (selected_) {
			auto it = packs_.find(*selected_);
			if (it != packs_.end() && it->second.manifest)
				name = G(it->second.manifest->name);
			if (name.is_empty()) name = G(sk::format_id(*selected_));
		}
		update_locker_banner(locker, name);
	}
	notify_status_if_changed();
}

void JVModelLoader::_ready() {
	set_process(true);
	set_process_priority(10000);
	if (RenderingServer *rs = RenderingServer::get_singleton()) {
		if (!rs->is_connected("frame_pre_draw", callable_mp(this, &JVModelLoader::_on_frame_pre_draw)))
			rs->connect("frame_pre_draw", callable_mp(this, &JVModelLoader::_on_frame_pre_draw));
	}
	JVModelTab *tab = memnew(JVModelTab);
	tab->set_name("JvmFullModelTab");
	add_child(tab);
}

void JVModelLoader::_on_frame_pre_draw() {


	if (!follow_.applied) return;
	for (auto &r : rigs_) {
		if (!r.grafted) continue;
		Node *rig = rig_of(r);
		Node *player = rig ? rig->get_parent() : nullptr;
		while (player && !jvm::script_is(player, "Player")) player = player->get_parent();
		const bool locker = is_locker_preview(player);
		for (ObjectID gid : r.grafts) {
			if (VisualInstance3D *vi = inst<VisualInstance3D>(gid)) {
				vi->set_visible(true);
				if (locker) vi->set_layer_mask(vi->get_layer_mask() | 1u);
			}
		}
		CtxGuard g(this, r);
		sweep(r);
	}
}

void JVModelLoader::_process(double delta) {
	JVM_PROF("models.process");
	if (settings_dirty_ && (settings_t_ -= delta) <= 0.0) save_settings_now();
	hook_check_t_ -= delta;
	if (hook_check_t_ <= 0.0) { hook_check_t_ = 1.0; install_hook(); }
	follow_.sync(this);
	if (!follow_.applied) {
		for (size_t i = 0; i < rigs_.size();) { if (!inst<Node>(rigs_[i].scene)) rigs_.erase(rigs_.begin() + i); else i++; }
		return;
	}
	for (auto &r : rigs_) r.eligible_t += delta;
	_service();
	for (auto &r : rigs_) {
		if (!r.grafted) continue;
		bool alive = true;
		for (ObjectID id : r.grafts) {
			Node *n = inst<Node>(id);
			if (!n || !n->is_inside_tree()) { alive = false; break; }
		}
		CtxGuard g(this, r);
		if (!alive) { ungraft(r, true); r.remote.reset(); continue; }
		sweep(r);
		update_tint(r);
		update_arms(r);
	}
}

void JVModelLoader::_exit_tree() {
	if (RenderingServer *rs = RenderingServer::get_singleton()) {
		if (rs->is_connected("frame_pre_draw", callable_mp(this, &JVModelLoader::_on_frame_pre_draw)))
			rs->disconnect("frame_pre_draw", callable_mp(this, &JVModelLoader::_on_frame_pre_draw));
	}
	if (SceneTree *t = get_tree())
		if (t->is_connected("node_added", callable_mp(this, &JVModelLoader::_on_node_added)))
			t->disconnect("node_added", callable_mp(this, &JVModelLoader::_on_node_added));
	flush_settings();
	if (follow_.applied) { follow_.applied = false; revert(); }
	if (rig_script_.is_valid() && hook_.is_valid()) {
		Variant cur = jvm_models_io().get("player.model", rig_script_, "model_hook");
		if (cur.get_type() == Variant::CALLABLE && Callable(cur) == hook_) jvm_models_io().set_undo("player.model", rig_script_.ptr(), "model_hook", chained_.is_valid() ? Variant(chained_) : Variant(Callable()));
	}
}


int64_t JVModelLoader::get_graft_count() const {
	int64_t n = 0;
	for (auto &r : rigs_) n += r.grafted ? 1 : 0;
	return n;
}

Array JVModelLoader::get_rig_report() const {
	Array a;
	for (auto &r : rigs_) {
		Dictionary d;
		Node *rig = rig_of(r);
		d["rig"] = rig && rig->is_inside_tree() ? String(rig->get_path()) : String("?");
		d["grafted"] = r.grafted;
		d["eligible"] = r.eligible;
		d["failed"] = r.failed;
		if (r.failed) d["reason"] = r.fail_reason;
		d["parts"] = int64_t(r.grafts.size());
		d["hidden"] = int64_t(r.hidden.size());
		d["materials"] = int64_t(r.mats.size());
		d["rest_error"] = r.rest_error;
		if (r.remote) d["remote"] = G(sk::format_id(*r.remote));
		a.push_back(d);
	}
	return a;
}

Dictionary JVModelLoader::get_status_brief() const { return follow_.status(); }

Dictionary JVModelLoader::get_status() const {
	Dictionary s = follow_.status();
	int n = 0;
	for (auto &[id, p] : packs_) n += p.is_model ? 1 : 0;
	s["packs"] = int64_t(n);
	s["content_folder"] = "resources/packs";
	s["legacy_models_folder"] = legacy_layout_;
	if (selected_) {
		Dictionary e;
		e["id"] = G(sk::format_id(*selected_));
		auto it = packs_.find(*selected_);
		if (it != packs_.end()) {
			e["pack"] = it->second.folder;
			if (it->second.parsed.spec) { e["name"] = G(it->second.parsed.spec->name); e["model"] = G(it->second.parsed.spec->model); }
		}
		s["equipped"] = e;
	} else s["equipped"] = Variant();
	s["model"] = model_state_;
	s["hook"] = hook_state_;
	s["hook_chained"] = chained_.is_valid();
	s["rigs"] = int64_t(rigs_.size());
	s["applied"] = get_graft_count();
	s["grafts_total"] = grafts_done_;
	s["fallbacks"] = fallbacks_;
	{
		const md::Spec *sp = spec();
		Dictionary a;
		a["mode"] = sp && sp->arms.follow ? "follow_hands" : "model";
		int64_t on = 0;
		double gap = 0;
		for (auto &r : rigs_) if (r.grafted && r.arms_on) { on++; gap = std::max(gap, std::max(r.arm_gap[0], r.arm_gap[1])); }
		a["rigs"] = on;
		a["max_stretch"] = gap;
		s["arms"] = a;
	}
	if (!last_error_.is_empty()) s["last_error"] = last_error_;
	Array w;
	for (auto &x : warnings_) w.push_back(G(x));
	s["warnings"] = w;
	return s;
}


static constexpr const char *kSettingsFile = "model_settings.json";

static bool write_atomic(const String &path, const String &text) {
	String tmp = path + String(".tmp");
	{
		Ref<FileAccess> f = FileAccess::open(tmp, FileAccess::WRITE);
		if (f.is_null()) return false;
		f->store_string(text);
	}
	if (DirAccess::rename_absolute(tmp, path) == OK) return true;
	DirAccess::remove_absolute(path);
	return DirAccess::rename_absolute(tmp, path) == OK;
}

static String hex_of(const Color &c) { float f[4] = { c.r, c.g, c.b, 1 }; return G(md::format_color(f)); }

void JVModelLoader::load_settings() {
	settings_ = Dictionary();
	String path = jvm::content::model_settings_read_path();
	if (!FileAccess::file_exists(path)) return;
	Variant v = jvm::load_json(path);
	if (v.get_type() != Variant::DICTIONARY) { log_once("settings", String(kSettingsFile) + " is not a JSON object; ignored", true); return; }
	Variant m = Dictionary(v).get("models", Dictionary());
	if (m.get_type() != Variant::DICTIONARY) return;
	Dictionary md_ = m;
	Array keys = md_.keys();
	for (int i = 0; i < keys.size(); i++) if (md_[keys[i]].get_type() == Variant::DICTIONARY) settings_[String(keys[i]).to_upper()] = Dictionary(md_[keys[i]]).duplicate();
}

void JVModelLoader::apply_settings(Pack &p) {
	if (!p.manifest) return;
	md::Spec s = *p.manifest;
	String id = G(sk::format_id(s.id));
	if (settings_.has(id)) {
		Dictionary d = settings_[id];
		md::UserSettings u;
		auto str = [&](const char *k) -> std::optional<std::string> { return d.has(k) && d[k].get_type() == Variant::STRING ? std::optional<std::string>(S(String(d[k]))) : std::nullopt; };
		u.color = str("color"); u.game_hands = str("game_hands"); u.apply_to = str("apply_to");
		std::vector<std::string> w;
		md::apply_user_settings(s, u, w);
		for (auto &x : w) { std::string m = "pack '" + S(p.folder) + "': " + x; warnings_.push_back(m); log_once("settings", G(m), true); }
	}
	p.parsed.spec = s;
}

void JVModelLoader::save_settings_now() {
	settings_dirty_ = false;
	Dictionary root;
	root["version"] = 1;
	root["models"] = settings_;
	String path = jvm::content::model_settings_write_path();
	if (!write_atomic(path, JSON::stringify(root, "\t"))) jvm::warn(kTag, "could not write " + path);
}

void JVModelLoader::flush_settings() { if (settings_dirty_) save_settings_now(); }

bool JVModelLoader::write_equipped_model(const String &id) {


	String path = jvm::content::equipped_path();
	Dictionary d;
	if (FileAccess::file_exists(path)) {
		Variant v = jvm::load_json(path);
		if (v.get_type() != Variant::DICTIONARY) { jvm::warn(kTag, "equipped.json is not a JSON object; model not saved (fix or delete the file)"); return false; }
		d = v;
	}
	d["model"] = id;
	if (!write_atomic(path, JSON::stringify(d, "\t"))) { jvm::warn(kTag, "could not write " + path); return false; }
	jvm::log(kTag, "locker: full model set to " + (id.is_empty() ? String("none (vanilla)") : id));
	return true;
}

Array JVModelLoader::get_model_list() const {
	Array a;
	for (auto &[id, p] : packs_) {
		if (!p.is_model) continue;
		Dictionary d;
		d["id"] = G(sk::format_id(id));
		d["folder"] = p.folder;
		d["usable"] = p.parsed.spec.has_value();
		if (p.manifest) { d["name"] = G(p.manifest->name); d["model"] = G(p.manifest->model); d["ext"] = G(p.manifest->ext); }
		else d["name"] = p.folder;
		if (!p.parsed.errors.empty()) d["error"] = G(md::join(p.parsed.errors, "; "));
		a.push_back(d);
	}
	return a;
}

String JVModelLoader::get_saved_model() const { return saved_ ? G(sk::format_id(*saved_)) : String(); }
String JVModelLoader::get_shown_model() const { return selected_ ? G(sk::format_id(*selected_)) : String(); }

bool JVModelLoader::preview_model(const String &id) {
	flush_settings();
	preview_on_ = true;
	preview_id_.reset();
	if (!id.is_empty()) preview_id_ = sk::parse_id(S(id));
	return reload_config();
}

bool JVModelLoader::apply_model(const String &id) {
	flush_settings();
	if (!write_equipped_model(id.to_upper())) return false;
	preview_on_ = false;
	reload_config();
	return true;
}

void JVModelLoader::end_preview() {
	flush_settings();
	if (!preview_on_) return;
	preview_on_ = false;
	reload_config();
}

Dictionary JVModelLoader::get_model_settings(const String &id_in) const {
	Dictionary d;
	auto id = sk::parse_id(S(id_in));
	if (!id) return d;
	auto it = packs_.find(*id);
	if (it == packs_.end() || !it->second.manifest || !it->second.parsed.spec) return d;
	const md::Spec &m = *it->second.manifest, &s = *it->second.parsed.spec;
	auto hands = [](md::Hands h) { return String(h == md::Hands::Keep ? "keep" : h == md::Hands::Hide ? "hide" : "tint"); };
	auto target = [](md::Target t) { return String(t == md::Target::All ? "all" : "local"); };
	String key = G(sk::format_id(*id));
	Dictionary u = settings_.has(key) ? Dictionary(settings_[key]) : Dictionary();
	String def;
	if (m.has_color) def = G(md::format_color(m.color));
	else if (selected_ == id && have_template_own_) def = hex_of(template_own_);
	d["id"] = key;
	d["default_color"] = def;
	d["color"] = s.has_color ? G(md::format_color(s.color)) : def;
	d["custom"] = u.has("color");
	d["game_hands"] = hands(s.hands);
	d["game_hands_default"] = hands(m.hands);
	d["apply_to"] = target(s.target);
	d["apply_to_default"] = target(m.target);
	d["ext"] = G(m.ext);
	d["model"] = G(m.model);
	d["shown"] = selected_ == id;
	return d;
}

bool JVModelLoader::set_model_color(const String &id_in, const Color &c) {
	auto id = sk::parse_id(S(id_in));
	if (!id || !packs_.count(*id) || !packs_[*id].manifest) return false;
	String key = G(sk::format_id(*id));
	Dictionary u = settings_.has(key) ? Dictionary(settings_[key]) : Dictionary();
	u["color"] = hex_of(c);
	settings_[key] = u;
	settings_dirty_ = true; settings_t_ = 0.5;
	apply_settings(packs_[*id]);
	if (selected_ == id) for (auto &r : rigs_) if (r.grafted && !r.remote) paint(r, true);
	return true;
}

bool JVModelLoader::reset_model_color(const String &id_in) {
	auto id = sk::parse_id(S(id_in));
	if (!id || !packs_.count(*id) || !packs_[*id].manifest) return false;
	String key = G(sk::format_id(*id));
	if (settings_.has(key)) {
		Dictionary u = settings_[key];
		u.erase("color");
		if (u.is_empty()) settings_.erase(key); else settings_[key] = u;
	}
	settings_dirty_ = true; settings_t_ = 0.0;
	apply_settings(packs_[*id]);
	if (selected_ == id) for (auto &r : rigs_) if (r.grafted && !r.remote) paint(r, true);
	return true;
}

bool JVModelLoader::set_model_option(const String &id_in, const String &key_in, const String &value) {
	auto id = sk::parse_id(S(id_in));
	if (!id || !packs_.count(*id) || !packs_[*id].manifest) return false;
	if (key_in != "game_hands" && key_in != "apply_to") return false;
	String key = G(sk::format_id(*id));
	Dictionary u = settings_.has(key) ? Dictionary(settings_[key]) : Dictionary();
	if (value.is_empty()) u.erase(key_in); else u[key_in] = value;
	if (u.is_empty()) settings_.erase(key); else settings_[key] = u;
	settings_dirty_ = true; settings_t_ = 0.0;
	apply_settings(packs_[*id]);
	if (selected_ == id) for (auto &r : rigs_) { if (r.grafted && !r.remote) ungraft(r, true); r.eligible_t = 1e9; }
	return true;
}


void JVModelLoader::swap_ctx(Ctx &c) {
	std::swap(selected_, c.sel);
	std::swap(template_scene_, c.scene);
	std::swap(template_node_, c.node);
	std::swap(template_own_, c.own);
	std::swap(have_template_own_, c.have_own);
	std::swap(model_path_, c.path);
	std::swap(model_state_, c.state);
	std::swap(grey_cache_, c.grey);
	std::swap(avg_cache_, c.avg);
}

JVModelLoader::CtxGuard::CtxGuard(JVModelLoader *loader, const Rig &r) : m(loader) {
	if (!r.remote) return;
	auto it = m->remote_tmpl_.find(*r.remote);
	if (it == m->remote_tmpl_.end()) return;
	c = &it->second;
	m->swap_ctx(*c);
}
JVModelLoader::CtxGuard::~CtxGuard() { if (c) m->swap_ctx(*c); }

bool JVModelLoader::ensure_remote_template(sk::SkinId id) {
	auto it = remote_tmpl_.find(id);
	if (it != remote_tmpl_.end()) return it->second.scene.is_valid() || it->second.node;
	auto p = packs_.find(id);
	if (p == packs_.end() || !p->second.is_model || !p->second.parsed.spec) return false;
	Ctx &c = remote_tmpl_[id];
	c.sel = id;
	swap_ctx(c);
	load_template();
	swap_ctx(c);
	jvm::log(kTag, "other player's model " + G(sk::format_id(id)) + ": " + c.state);
	return c.scene.is_valid() || c.node;
}

void JVModelLoader::free_remote_templates() {
	for (auto &[id, c] : remote_tmpl_) { swap_ctx(c); free_template(); swap_ctx(c); }
	remote_tmpl_.clear();
}

std::optional<sk::SkinId> JVModelLoader::remote_for(Node *rig) {
	Node *player = rig ? rig->get_parent() : nullptr;
	while (player && !jvm::script_is(player, "Player")) player = player->get_parent();
	if (!player || is_locker_preview(player)) return std::nullopt;
	Dictionary cfg;
	Variant sc = jvm::load_json(jvm::install_dir().path_join("texture_sync.json"));
	if (sc.get_type() == Variant::DICTIONARY) cfg = sc;
	OS *os = OS::get_singleton();
	if (os->is_debug_build() && !os->get_environment("JV_TEXTURE_SYNC").is_empty()) {
		Variant t = jvm::load_json(os->get_environment("JV_TEXTURE_SYNC"));
		if (t.get_type() == Variant::DICTIONARY) cfg = t;
	}
	StringName pp_name = String(cfg.get("players_property", "_peer_players"));
	for (Node *a = player->get_parent(); a; a = a->get_parent()) {
		Variant pp = jvm_models_io().get("scene.players", a, pp_name);
		if (pp.get_type() != Variant::DICTIONARY) continue;
		Dictionary d = pp;
		Array keys = d.keys();
		for (int i = 0; i < keys.size(); i++) {
			if (d[keys[i]].get_type() != Variant::OBJECT || d[keys[i]].operator Object *() != player) continue;
			if (keys[i].get_type() != Variant::INT) return std::nullopt;
			auto m = remote_models_.find(int32_t(int64_t(keys[i])));
			if (m == remote_models_.end()) return std::nullopt;
			auto p = packs_.find(m->second);
			if (p == packs_.end() || !p->second.is_model || !p->second.parsed.spec) return std::nullopt;
			return m->second;
		}
	}
	return std::nullopt;
}

bool JVModelLoader::offer_local(String &id, String &dir) const {
	if (!follow_.applied || !saved_) return false;
	auto it = packs_.find(*saved_);
	if (it == packs_.end() || !it->second.is_model || !it->second.parsed.spec) return false;
	id = G(sk::format_id(*saved_));
	dir = it->second.dir;
	return true;
}

Dictionary JVModelLoader::installed_dirs() const {
	Dictionary d;
	for (auto &[id, p] : packs_) if (p.is_model) d[G(sk::format_id(id))] = p.dir;
	return d;
}

void JVModelLoader::set_remote_models(const Dictionary &peer_to_id) {
	std::map<int32_t, sk::SkinId> m;
	Array keys = peer_to_id.keys();
	for (int i = 0; i < keys.size(); i++)
		if (auto id = sk::parse_id(S(String(peer_to_id[keys[i]])))) m[int32_t(int64_t(keys[i]))] = *id;
	if (m == remote_models_) return;
	remote_models_ = m;
	for (auto &r : rigs_) r.eligible_t = 1e9;
}

Dictionary JVModelLoader::get_remote_models() const {
	Dictionary d;
	for (auto &[peer, id] : remote_models_) d[peer] = G(sk::format_id(id));
	return d;
}

bool godot::jvm_models_local_offer(String &id, String &dir) { return g_models && g_models->offer_local(id, dir); }
Dictionary godot::jvm_models_installed_dirs() { return g_models ? g_models->installed_dirs() : Dictionary(); }
void godot::jvm_models_rescan() { if (g_models) g_models->reload_config(); }
void godot::jvm_models_set_remote(const Dictionary &d) { if (g_models) g_models->set_remote_models(d); }
