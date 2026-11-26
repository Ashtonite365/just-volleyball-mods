#include "unified_loader.hpp"
#include "../common/jv_prof.hpp"

#include "../common/jvm_common.hpp"
#include "../models/model_loader.hpp"
#include "../textures/texture_loader.hpp"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;



static const char *const kLoaderCaps[] = { "gate.mode", "config.files", "scene.current", "scene.stage", "packs.query", "goofy.packs" };
static jvm::GameIO &IO() {
	static jvm::GameIO io = [] {
		jvm::GameIO g;
		g.mod = "loader";
		g.writes_need_open = false;
		for (const char *c : kLoaderCaps) g.caps.push_back(c);
		return g;
	}();
	return io;
}
namespace ld = jvm::loader;

static std::string S(const String &s) { return std::string(s.utf8().get_data()); }
static String G(const std::string &s) { return String::utf8(s.c_str()); }
static constexpr const char *kTag = "loader";
static constexpr double kTick = 0.25;

JVLoader::JVLoader() { set_process_mode(PROCESS_MODE_ALWAYS); }

void JVLoader::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_status"), &JVLoader::get_status);
	ClassDB::bind_method(D_METHOD("get_bridge_status"), &JVLoader::get_bridge_status);
	ClassDB::bind_method(D_METHOD("is_loader_map", "stage_id"), &JVLoader::is_loader_map);
	ClassDB::bind_method(D_METHOD("get_active_map"), &JVLoader::get_active_map);
	ClassDB::bind_method(D_METHOD("get_map_goofy_block", "stage_id"), &JVLoader::get_map_goofy_block);
	ClassDB::bind_method(D_METHOD("refresh"), &JVLoader::refresh);
	ClassDB::bind_method(D_METHOD("reload_config"), &JVLoader::reload_config);
}

Node *JVLoader::texture_node() const { return Object::cast_to<Node>(ObjectDB::get_instance(textures_)); }
Node *JVLoader::model_node() const { return Object::cast_to<Node>(ObjectDB::get_instance(models_)); }
Dictionary JVLoader::goofy_info() const {
	if (!is_inside_tree()) return Dictionary();
	Variant v = IO().core("goofy_bridge_info");
	return v.get_type() == Variant::DICTIONARY ? Dictionary(v) : Dictionary();
}



void JVLoader::spawn_subsystems() {
	if (spawned_) return;
	spawned_ = true;
	Window *root = get_tree()->get_root();
	Node *gate = jvm::find_gate(this);
	auto spawn = [&](Node *n, const char *name, const char *lib) -> ObjectID {
		if (Node *old = root->get_node_or_null(NodePath(name))) {

			jvm::warn(kTag, String(name) + " already present (legacy library?) - using it");
			if (gate) gate->call("register_library", lib, "loaded", "legacy library");
			memdelete(n);
			return ObjectID(old->get_instance_id());
		}
		n->set_name(name);
		root->add_child(n);
		if (gate) gate->call("register_library", lib, "loaded", "subsystem of jvm_loader");
		return ObjectID(n->get_instance_id());
	};
	textures_ = spawn(memnew(JVTextureLoader), "JVTextureLoader", "textures");
	models_ = spawn(memnew(JVModelLoader), "JVModelLoader", "models");
	jvm::log(kTag, String("unified loader ") + kVersion + ": texture + model subsystems up, stage + goofy bridge ticking");
}

void JVLoader::_ready() {

	callable_mp(this, &JVLoader::spawn_subsystems).call_deferred();
	set_process(true);
}

void JVLoader::_exit_tree() {
	if (!goofy_info().is_empty()) {
		clear_now();
		if (bs_.told) IO().core("goofy_loader_map_set", String());
	}
	bs_ = jvm::loader::BridgeState();


	for (ObjectID id : { textures_, models_ })
		if (Node *n = Object::cast_to<Node>(ObjectDB::get_instance(id)))
			if (!n->is_queued_for_deletion()) n->queue_free();
	textures_ = ObjectID(); models_ = ObjectID();
}

void JVLoader::_process(double delta) {
	JVM_PROF("loader.process");
	tick_t_ += delta;
	if (tick_t_ < kTick) return;
	tick_t_ = 0.0;
	update_stage();
	update_bridge();
}

void JVLoader::refresh() { update_stage(); update_bridge(); }

bool JVLoader::reload_config() {
	bool any = false;
	if (Node *t = texture_node()) any = bool(t->call("reload_config")) || any;
	if (Node *m = model_node()) any = bool(m->call("reload_config")) || any;
	block_cache_.clear();
	refresh();
	return any;
}


bool JVLoader::is_loader_map(const String &stage_id) const {
	Node *t = texture_node();
	if (!t || stage_id.strip_edges().is_empty()) return false;
	Dictionary info = t->call("pack_info", stage_id.strip_edges());
	return String(info.get("type", "")) == "stage";
}

Dictionary JVLoader::get_map_goofy_block(const String &stage_id) {
	Node *t = texture_node();
	if (!t) return Dictionary();
	Dictionary info = t->call("pack_info", stage_id.strip_edges());
	if (String(info.get("type", "")) != "stage") return Dictionary();
	String id = info.get("id", "");
	String file = jvm::mods_dir(texture_pack_rel()).path_join(String(info.get("pack", ""))).path_join("skin.json");
	uint64_t mt = FileAccess::file_exists(file) ? FileAccess::get_modified_time(file) : 0;
	if (block_cache_.has(id)) {
		Dictionary c = block_cache_[id];
		if (uint64_t(int64_t(c["mtime"])) == mt && String(c["file"]) == file) return c["block"];
	}
	Dictionary block;
	Variant man = jvm::load_json(file);
	if (man.get_type() == Variant::DICTIONARY) {
		Variant g = Dictionary(man).get("goofy", Variant());
		if (g.get_type() == Variant::DICTIONARY) block = Dictionary(g).duplicate(true);
		else if (g.get_type() != Variant::NIL) jvm::warn(kTag, "stage " + id + ": \"goofy\" must be an object { ... } (ignored)");
	}
	Dictionary c; c["mtime"] = int64_t(mt); c["file"] = file; c["block"] = block;
	block_cache_[id] = c;
	return block;
}

static bool scene_has_court(Node *scene) {
	if (!scene) return false;
	if (IO().node("scene.stage", scene, "Court") || IO().node("scene.stage", scene, "Stage/Court")) return true;
	return scene->find_child("Court", true, false) != nullptr;
}

void JVLoader::update_stage() {
	String was = active_map_;
	active_map_ = "";
	Node *t = texture_node();
	if (!t) { active_reason_ = "texture subsystem not running"; }
	else if (!jvm::gate_cosmetic_active(this)) { active_reason_ = "cosmetic tier closed (" + jvm::gate_reason(this) + ")"; }
	else {
		String picked = t->call("get_picked_stage");
		if (picked.is_empty()) active_reason_ = "stock map (no loader stage picked)";
		else if (!is_loader_map(picked)) active_reason_ = "picked stage " + picked + " is not a loaded stage pack";
		else if (!scene_has_court(jvm::current_scene(this))) active_reason_ = "no court in the current scene";
		else { active_map_ = picked; active_reason_ = "loader map " + picked + " in play"; }
	}
	if (was != active_map_) jvm::log(kTag, "map: " + (active_map_.is_empty() ? String("(none) - ") + active_reason_ : active_map_ + " loaded"));
}


void JVLoader::clear_now() {
	if (bs_.applied_pack.empty()) return;
	IO().core("goofy_pack_clear", G(bs_.applied_pack));
	jvm::log(kTag, "goofy: map " + G(bs_.applied_pack) + " goofy section removed (map left)");
	bs_.applied_pack.clear();
	bs_.applied_sig.clear();
	clears_++;
}

void JVLoader::update_bridge() {
	const Dictionary gi = goofy_info();
	bool usable = !gi.is_empty();
	goofy_version_ = usable ? String(gi.get("version", "")) : String();
	usable = usable && ld::api_compatible(S(goofy_version_));
	ObjectID gid = usable ? ObjectID(uint64_t(int64_t(gi.get("id", 0)))) : ObjectID();
	if (gid != goofy_id_) {
		bs_.told = false; bs_.told_map.clear(); bs_.applied_pack.clear(); bs_.applied_sig.clear();
		goofy_id_ = gid;
	}
	goofy_enabled_ = usable && bool(gi.get("enabled", false));
	bs_.goofy_usable = usable;
	bs_.map = S(active_map_);
	Dictionary block;
	bs_.has_block = false;
	bs_.block_sig.clear();
	if (!active_map_.is_empty()) {
		block = get_map_goofy_block(active_map_);
		bs_.has_block = !block.is_empty();
		if (block_cache_.has(active_map_)) bs_.block_sig = std::to_string(int64_t(Dictionary(block_cache_[active_map_])["mtime"]));
	}
	ld::Decision d = ld::decide(bs_);
	bridge_why_ = d.why;
	if (!usable) return;
	if (!d.clear_pack.empty()) clear_now();
	if (d.set_map) {
		IO().core("goofy_loader_map_set", active_map_);
		bs_.told = true; bs_.told_map = bs_.map; map_calls_++;
	}
	if (d.apply) {
		int version = int(block.get("version", 1));
		Dictionary params = block.duplicate(true);
		params.erase("version");
		Variant r = IO().core("goofy_pack_apply", active_map_, params, int64_t(version));
		last_result_ = r.get_type() == Variant::DICTIONARY ? Dictionary(r) : Dictionary();
		bs_.applied_pack = bs_.map; bs_.applied_sig = bs_.block_sig;
		applies_++;
		Array errs = last_result_.get("errors", Array());
		Array ign = last_result_.get("ignored", Array());
		jvm::log(kTag, "goofy: map " + active_map_ + " goofy section handed to goofy: ok=" + String(last_result_.get("ok", false) ? "true" : "false") +
				" applied=" + JSON::stringify(last_result_.get("applied", Array())) + " ignored=" + JSON::stringify(ign) +
				" live=" + String(last_result_.get("active", false) ? "yes" : "no (goofy disabled here)"));
		if (!errs.is_empty()) jvm::warn(kTag, "goofy: map " + active_map_ + " goofy section errors: " + JSON::stringify(errs) + " (map loads normally)");
	}
}

Dictionary JVLoader::get_bridge_status() const {
	Dictionary b;
	b["goofy_found"] = bs_.goofy_usable;
	b["goofy_api_version"] = goofy_version_;
	b["goofy_enabled"] = goofy_enabled_;
	b["map"] = active_map_;
	b["told_map"] = G(bs_.told_map);
	b["has_goofy_section"] = bs_.has_block;
	b["applied_pack"] = G(bs_.applied_pack);
	b["state"] = bs_.applied_pack.empty() ? "idle" : "applied";
	b["why"] = bridge_why_;
	b["last_result"] = last_result_;
	b["applies"] = applies_;
	b["clears"] = clears_;
	b["set_loader_map_calls"] = map_calls_;
	return b;
}

Dictionary JVLoader::get_status() const {
	Dictionary s;
	Node *t = texture_node();
	Node *m = model_node();
	bool any_active = false;
	Dictionary subs;
	auto sub = [&](Node *n, const char *key) {
		Dictionary d;



		if (n && n->has_method("get_status_brief")) {
			d = n->call("get_status_brief");
			any_active = any_active || String(d.get("state", "")) == "active";
		} else if (n && n->has_method("get_status")) {
			Dictionary full = n->call("get_status");
			d["state"] = full.get("state", "?");
			d["reason"] = full.get("reason", "");
			any_active = any_active || String(d["state"]) == "active";
		} else { d["state"] = "not-loaded"; }
		subs[key] = d;
	};
	sub(t, "textures");
	sub(m, "models");
	Dictionary st; st["state"] = active_map_.is_empty() ? "idle" : "map"; st["map"] = active_map_; st["reason"] = active_reason_;
	subs["stage"] = st;
	subs["goofy_bridge"] = get_bridge_status();
	s["state"] = any_active ? "active" : "blocked";
	s["reason"] = any_active ? String("unified loader ") + kVersion : String(Dictionary(subs["textures"]).get("reason", "subsystems not running"));
	s["tier"] = "cosmetic";
	s["version"] = kVersion;
	s["subsystems"] = subs;
	return s;
}
