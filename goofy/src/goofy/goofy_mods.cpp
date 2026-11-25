#include "goofy_mods.hpp"
#include "../common/jv_prof.hpp"

#include <cmath>

#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/scene_tree.hpp>

using namespace godot;

static const char *kMaxHitsId = "jvm_max_hits";
static const char *kNoDoubleId = "jvm_no_double";
static const char *kMod = "goofy";

static const char *kGoofyCaps[] = {
	"gate.mode", "config.files", "scene.balls", "scene.players", "scene.rally", "net.state", "net.lobby_data",
	"ball.physics", "ball.model", "ball.scale", "ball.flight", "ball.struck", "ball.events",
	"player.scale", "player.touched", "player.events", "player.feel", "player.tuning", "player.input_state", "player.look",
	"rules.touch_count", "rules.double_hit", "rules.modifiers_panel", "engine.time_scale", "engine.overrides",
	"models.query", "net.packets", "goofy.host_sync", "gameplay.settings", "game.rules", "player.consts",
};
static const char *kGoofyApi = "1.3";




jvm::GameIO &jvm_goofy_io() {
	static jvm::GameIO io = [] {
		jvm::GameIO g;
		g.mod = kMod;
		g.api_version = kGoofyApi;
		for (const char *c : kGoofyCaps) g.caps.push_back(c);
		g.writes_need_open = true;
		return g;
	}();
	return io;
}
static jvm::GameIO &IO() { return jvm_goofy_io(); }

JVGoofyMods::JVGoofyMods() {
	follow_.mod = kMod;
	follow_.access = "gameplay";
	follow_.api_version = kGoofyApi;
	for (const char *c : kGoofyCaps) follow_.caps.push_back(c);
}

void JVGoofyMods::_bind_methods() {
	JVM_PROF_TAP_BIND(JVGoofyMods);
	ClassDB::bind_method(D_METHOD("reload_config"), &JVGoofyMods::reload_config);
	ClassDB::bind_method(D_METHOD("_on_access_changed", "access", "open", "reason"), &JVGoofyMods::_on_access_changed);
	ClassDB::bind_method(D_METHOD("_on_node_added", "node"), &JVGoofyMods::_on_node_added);
	ClassDB::bind_method(D_METHOD("_on_player_touched", "kind", "who"), &JVGoofyMods::_on_player_touched);
	ClassDB::bind_method(D_METHOD("_on_panel_changed", "key", "value"), &JVGoofyMods::_on_panel_changed);
	ClassDB::bind_method(D_METHOD("get_status"), &JVGoofyMods::get_status);
	ClassDB::bind_method(D_METHOD("validate_config", "config"), &JVGoofyMods::validate_config);
	ClassDB::bind_method(D_METHOD("set_max_hits", "v"), &JVGoofyMods::set_max_hits);
	ClassDB::bind_method(D_METHOD("get_max_hits"), &JVGoofyMods::get_max_hits);
	ClassDB::bind_method(D_METHOD("set_no_double_hit", "v"), &JVGoofyMods::set_no_double_hit);
	ClassDB::bind_method(D_METHOD("get_no_double_hit"), &JVGoofyMods::get_no_double_hit);
	ClassDB::bind_static_method("JVGoofyMods", D_METHOD("rules_text", "max_hits", "no_double"), &JVGoofyMods::rules_text);
	ClassDB::bind_method(D_METHOD("_on_gs_changed", "id", "value", "source"), &JVGoofyMods::_on_gs_changed);
	ClassDB::bind_method(D_METHOD("_gs_migrate", "from", "values"), &JVGoofyMods::_gs_migrate);
	ClassDB::bind_method(D_METHOD("get_rules"), &JVGoofyMods::get_rules);

	ClassDB::bind_method(D_METHOD("api_version"), &JVGoofyMods::api_version);
	ClassDB::bind_method(D_METHOD("is_enabled"), &JVGoofyMods::is_enabled);
	ClassDB::bind_method(D_METHOD("is_physics_allowed"), &JVGoofyMods::is_physics_allowed);
	ClassDB::bind_method(D_METHOD("get_schema"), &JVGoofyMods::get_schema);
	ClassDB::bind_method(D_METHOD("get_param", "id"), &JVGoofyMods::get_param);
	ClassDB::bind_method(D_METHOD("get_params"), &JVGoofyMods::get_params);
	ClassDB::bind_method(D_METHOD("get_stock", "id"), &JVGoofyMods::get_stock);
	ClassDB::bind_method(D_METHOD("get_layers"), &JVGoofyMods::get_layers);
	ClassDB::bind_method(D_METHOD("get_force_defaults"), &JVGoofyMods::get_force_defaults);
	ClassDB::bind_method(D_METHOD("get_ball_mode"), &JVGoofyMods::get_ball_mode);
	ClassDB::bind_method(D_METHOD("get_loader_map"), &JVGoofyMods::get_loader_map);
	ClassDB::bind_method(D_METHOD("apply_pack_params", "pack_id", "params", "version"), &JVGoofyMods::apply_pack_params, DEFVAL(1));
	ClassDB::bind_method(D_METHOD("clear_pack_params", "pack_id"), &JVGoofyMods::clear_pack_params);
	ClassDB::bind_method(D_METHOD("set_loader_map", "pack_id"), &JVGoofyMods::set_loader_map);
	ClassDB::bind_method(D_METHOD("set_override", "owner", "params", "priority", "scope"), &JVGoofyMods::set_override, DEFVAL(500), DEFVAL("session"));
	ClassDB::bind_method(D_METHOD("clear_override", "owner"), &JVGoofyMods::clear_override);
	ClassDB::bind_method(D_METHOD("set_ball_mode", "mode"), &JVGoofyMods::set_ball_mode);

	ClassDB::bind_method(D_METHOD("get_ball_schema"), &JVGoofyMods::get_ball_schema);
	ClassDB::bind_method(D_METHOD("get_player_schema"), &JVGoofyMods::get_player_schema);
	ClassDB::bind_method(D_METHOD("list_balls"), &JVGoofyMods::list_balls);
	ClassDB::bind_method(D_METHOD("get_ball_state", "index"), &JVGoofyMods::get_ball_state);
	ClassDB::bind_method(D_METHOD("set_ball_velocity", "index", "velocity"), &JVGoofyMods::set_ball_velocity);
	ClassDB::bind_method(D_METHOD("set_ball_curve", "index", "curve"), &JVGoofyMods::set_ball_curve);
	ClassDB::bind_method(D_METHOD("apply_ball_impulse", "index", "delta_v"), &JVGoofyMods::apply_ball_impulse);
	ClassDB::bind_method(D_METHOD("list_players"), &JVGoofyMods::list_players);
	ClassDB::bind_method(D_METHOD("get_player_state", "index"), &JVGoofyMods::get_player_state);
	ClassDB::bind_method(D_METHOD("get_player_cosmetics", "index"), &JVGoofyMods::get_player_cosmetics);
	ClassDB::bind_method(D_METHOD("set_player_override", "owner", "params", "priority", "scope"), &JVGoofyMods::set_player_override, DEFVAL(500), DEFVAL("session"));
	ClassDB::bind_method(D_METHOD("get_sync_status"), &JVGoofyMods::get_sync_status);
	ClassDB::bind_method(D_METHOD("player_consts_set", "name", "value", "source"), &JVGoofyMods::player_consts_set, DEFVAL("api"));
	ClassDB::bind_method(D_METHOD("player_consts_reset", "name", "source"), &JVGoofyMods::player_consts_reset, DEFVAL("api"));
	ClassDB::bind_method(D_METHOD("player_consts_get"), &JVGoofyMods::player_consts_get);
	ClassDB::bind_method(D_METHOD("player_consts_locked"), &JVGoofyMods::player_consts_locked);
	ClassDB::bind_method(D_METHOD("_on_sync_packet", "from", "packet"), &JVGoofyMods::_on_sync_packet);
	ClassDB::bind_method(D_METHOD("_on_sync_peer_gone", "id"), &JVGoofyMods::_on_sync_peer_gone);
	ClassDB::bind_method(D_METHOD("_on_ev0", "kind", "name", "id"), &JVGoofyMods::_on_ev0);
	ClassDB::bind_method(D_METHOD("_on_ev1", "a", "kind", "name", "id"), &JVGoofyMods::_on_ev1);
	ClassDB::bind_method(D_METHOD("_on_ev2", "a", "b", "kind", "name", "id"), &JVGoofyMods::_on_ev2);
	ClassDB::bind_method(D_METHOD("_on_ev3", "a", "b", "c", "kind", "name", "id"), &JVGoofyMods::_on_ev3);
	ADD_SIGNAL(MethodInfo("game_event", PropertyInfo(Variant::STRING, "kind"), PropertyInfo(Variant::STRING, "name"), PropertyInfo(Variant::INT, "index"), PropertyInfo(Variant::ARRAY, "args")));
	ADD_SIGNAL(MethodInfo("player_consts_notice", PropertyInfo(Variant::STRING, "notice")));
	ADD_SIGNAL(MethodInfo("sync_changed", PropertyInfo(Variant::STRING, "role"), PropertyInfo(Variant::INT, "host_values")));
	ADD_SIGNAL(MethodInfo("params_changed", PropertyInfo(Variant::ARRAY, "changed"), PropertyInfo(Variant::DICTIONARY, "effective")));
	ADD_SIGNAL(MethodInfo("layers_changed"));
	ADD_SIGNAL(MethodInfo("ball_mode_changed", PropertyInfo(Variant::STRING, "mode"), PropertyInfo(Variant::BOOL, "force_defaults")));
	ADD_SIGNAL(MethodInfo("physics_allowed_changed", PropertyInfo(Variant::BOOL, "allowed")));
}

static Variant convert_value(const Variant &v) {
	if (v.get_type() == Variant::ARRAY) {
		Array a = v;
		if (a.size() == 3) return Vector3(a[0], a[1], a[2]);
		if (a.size() == 4) return Color(a[0], a[1], a[2], a[3]);
	}
	return v;
}

String JVGoofyMods::rules_text(int max_hits, bool no_double) {
	return "mh=" + String::num_int64(jvm::rules::clamp_max_hits(max_hits)) + ";ndh=" + String(no_double ? "1" : "0");
}

bool JVGoofyMods::parse_rules_text(const String &t, int &max_hits, bool &no_double) {
	if (t.is_empty()) return false;
	PackedStringArray parts = t.split(";");
	bool any = false;
	for (int i = 0; i < parts.size(); i++) {
		PackedStringArray kv = parts[i].split("=");
		if (kv.size() != 2) continue;
		if (kv[0] == "mh" && kv[1].is_valid_int()) { max_hits = jvm::rules::clamp_max_hits(kv[1].to_int()); any = true; }
		if (kv[0] == "ndh") { no_double = kv[1] == "1"; any = true; }
	}
	return any;
}

static String goofy_range(const Dictionary &d, const String &k, double lo, double hi, const String &where = "") {
	if (!d.has(k) || d[k].get_type() == Variant::NIL) return "";
	if (d[k].get_type() != Variant::INT && d[k].get_type() != Variant::FLOAT) return where + k + " must be a number";
	double v = d[k];
	if (v < lo || v > hi) return where + k + " must be between " + String::num(lo) + " and " + String::num(hi) + " (got " + String::num(v) + ")";
	return "";
}

String JVGoofyMods::validate_config(const Dictionary &d) const {
	String e;
	if (!(e = goofy_range(d, "big_ball", 0.25, 6.0)).is_empty()) return e + " (1.0 = off)";
	if (!(e = goofy_range(d, "player_scale", 0.25, 4.0)).is_empty()) return e + " (1.0 = off)";
	if (!(e = goofy_range(d, "time_scale", 0.1, 4.0)).is_empty()) return e;
	if (!(e = goofy_range(d, "max_hits_per_team", 0, 10)).is_empty()) return e + " (0 = the game's rule)";
	if (d.has("max_hits_per_team") && double(d["max_hits_per_team"]) != std::floor(double(d["max_hits_per_team"]))) return "max_hits_per_team must be a whole number";
	if (d.has("no_double_hit") && d["no_double_hit"].get_type() != Variant::BOOL) return "no_double_hit must be true/false";
	if (d.has("ball_physics")) {
		if (d["ball_physics"].get_type() != Variant::DICTIONARY) return "ball_physics must be an object { ... }";
		Dictionary bp = d["ball_physics"];
		if (!(e = goofy_range(bp, "gravity_scale", 0.05, 5.0, "ball_physics.")).is_empty()) return e + " (1.0 = off)";
		if (!(e = goofy_range(bp, "bounce_scale", 0.0, 3.0, "ball_physics.")).is_empty()) return e + " (1.0 = off)";
	}
	if (d.has("ball_params")) {
		if (d["ball_params"].get_type() != Variant::DICTIONARY) return "ball_params must be an object { \"ball.gravity\": 9, ... }";
		std::map<std::string, jvm::goofy::Value> tmp;
		jvm::goofy::ParseResult r = parse_values(d["ball_params"], tmp);
		if (!r.errors.empty()) return String("ball_params: ") + r.errors[0].c_str();
		if (!r.ignored.empty()) return String("ball_params: unknown param ") + r.ignored[0].c_str() + " (see docs/GOOFY-BALL.md)";
	}
	if (d.has("player_params")) {
		if (d["player_params"].get_type() != Variant::DICTIONARY) return "player_params must be an object { \"player.move_speed\": 7, ... }";
		std::map<std::string, jvm::goofy::Value> tmp;
		jvm::goofy::ParseResult r = parse_values(d["player_params"], tmp);
		if (!r.errors.empty()) return String("player_params: ") + r.errors[0].c_str();
		if (!r.ignored.empty()) return String("player_params: unknown param ") + r.ignored[0].c_str() + " (see docs/GOOFY-PLAYER.md)";
	}
	if (d.has("overrides") && d["overrides"].get_type() != Variant::ARRAY) return "overrides must be a list [ ... ]";
	return "";
}

Dictionary JVGoofyMods::get_status() const {
	Dictionary d = follow_.status();
	Dictionary f;
	Object *a = api();
	f["time_scale"] = time_changed_ && a ? a->call("time_scale_get") : Variant("off");
	f["ball_physics"] = phys_orig_.size();
	f["rules"] = get_rules();
	f["ball_library"] = pushed_.size();
	f["physics_allowed"] = physics_allowed_;
	f["ball_mode"] = ball_mode_;
	f["force_defaults"] = stack_.force_defaults();
	f["loader_map"] = String(stack_.loader_map().c_str());
	f["layers"] = int(stack_.layers().size());
	f["ext_frames"] = ext_frames_;
	f["big_ball"] = big_ball_;
	f["player_scale"] = player_scale_;
	f["player_library"] = int(jvm::goofy::params().size()) - jvm::goofy::ball_param_count();
	f["sync"] = get_sync_status();
	f["scaled_nodes"] = int(scaled_.size());
	f["max_hits_per_team"] = max_hits_;
	f["no_double_hit"] = no_double_;
	f["rule_writes"] = rule_writes_;
	f["double_clears"] = double_clears_;
	f["panel_rows"] = panel_id_ != 0;
	f["match_off"] = match_off_;
	d["features"] = f;


	d["goofy_api_version"] = api_version();
	return d;
}

void JVGoofyMods::set_max_hits(int v) { max_hits_ = jvm::rules::clamp_max_hits(v); jvm::log("goofy", "max hits per team -> " + (max_hits_ ? String::num_int64(max_hits_) : String("off (game rule 3)"))); }
void JVGoofyMods::set_no_double_hit(bool v) { no_double_ = v; jvm::log("goofy", String("no double-hit foul -> ") + (v ? "on" : "off")); }

void JVGoofyMods::apply() {
	Object *a = api();
	if (!a) return;
	cfg_ = a->call("load_config", kMod, "goofy");
	Array rules = cfg_.get("overrides", Array());
	for (int r = 0; r < rules.size(); r++) {
		if (rules[r].get_type() != Variant::DICTIONARY) continue;
		Dictionary rule = rules[r];
		Dictionary set = rule.get("set", Dictionary());
		Array k = set.keys();
		for (int i = 0; i < k.size(); i++) set[k[i]] = convert_value(set[k[i]]);
	}
	a->call("overrides_set_rules", kMod, rules);
	override_count_ = rules.size();
	if (gs_registered_) gs_sync_legacy(cfg_);
	if (!gs_registered_ && cfg_.has("time_scale") && cfg_["time_scale"].get_type() != Variant::NIL) {
		double ts = CLAMP(double(cfg_["time_scale"]), 0.1, 4.0);
		cfg_ts_ = ts;
		time_changed_ = true;
		if (ts_synced_) {
			ts_client_orig_ = ts;
		} else {
			orig_time_scale_ = a->call("time_scale_get");
			a->call("time_scale_set", kMod, ts);
			jvm::log("goofy", "time_scale -> " + String::num(ts));
		}
	} else cfg_ts_ = 1.0;
	big_ball_ = gs_registered_ ? (use_local_gs_layer() ? gs_num_local("goofy.ball_size", 1.0) : gs_num("goofy.ball_size", 1.0)) : CLAMP(double(cfg_.get("big_ball", 1.0)), 0.25, 6.0);
	static bool rules_loaded = false;
	if (!rules_loaded && !gs_registered_) {
		max_hits_ = jvm::rules::clamp_max_hits(int(cfg_.get("max_hits_per_team", 0)));
		no_double_ = bool(cfg_.get("no_double_hit", false));
		rules_loaded = true;
	}
	pc_load_config();
	build_user_layer();
	physics_gate_tick();
	gs_apply();
	rescan_ = 1e9;
	int n = int(a->call("overrides_apply", kMod));
	jvm::log("goofy", "applied: big_ball x" + String::num(big_ball_) + ", player_scale x" + String::num(player_scale_) +
			", ball_physics " + String::num_int64(phys_orig_.size()) + " prop(s), " + String::num_int64(n) + " override(s), max_hits " +
			String::num_int64(max_hits_) + ", no_double_hit " + (no_double_ ? "on" : "off"));
	mark_work_dirty();
}

void JVGoofyMods::revert() {
	Object *a = api();
	if (time_changed_) {
		if (ts_synced_) ts_client_orig_ = orig_time_scale_;
		else if (a) a->call("time_scale_restore", kMod, orig_time_scale_);
		time_changed_ = false;
	}
	gs_time_scale_ = 1.0;
	apply_block_classic(false, true);
	streak_ = jvm::rules::Streak(); streak_rally_ = 0;
	if (a) a->call("overrides_revert", kMod);
	revert_scales();
	restore_library();
	if (pc_in_match_ && !reloading_) pc_match_end();
	if (physics_allowed_) { physics_allowed_ = false; emit_signal("physics_allowed_changed", false); }
	if (sync_role_ != "off" && !reloading_) set_sync_role("off");
	jvm::log("goofy", "reverted");
}








static double dressed_radius(Object *api, Node3D *ball, double fallback) {
	Node3D *model = api ? Object::cast_to<Node3D>(api->call("ball_get_model", kMod, ball).operator Object *()) : nullptr;
	if (model && model->is_inside_tree()) {
		Transform3D inv = model->get_global_transform().affine_inverse();
		TypedArray<Node> ms = model->find_children("*", "MeshInstance3D", true, false);
		AABB all; bool first = true;
		for (int i = 0; i < ms.size(); i++) {
			MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(Object::cast_to<Node>(ms[i]));
			if (!mi || mi->get_mesh().is_null()) continue;
			AABB b = (inv * mi->get_global_transform()).xform(mi->get_aabb());
			all = first ? b : all.merge(b); first = false;
		}
		if (!first && all.get_longest_axis_size() > 0.001) return all.get_longest_axis_size() * 0.5;
	}
	return fallback;
}

void JVGoofyMods::apply_scales() {
	Object *a = api();
	if (!a) return;

	{
		const double r_orig = phys_.is_valid() ? double(phys_orig_.has("radius") ? phys_orig_["radius"] : IO().get("ball.physics", phys_, "radius")) : 0.0;
		const double r_now = phys_.is_valid() ? double(IO().get("ball.physics", phys_, "radius")) : 0.0;


		if (r_orig > 0.0 && (r_now != r_orig || !scaled_.empty())) {
			Array nodes = a->call("find_balls", kMod);
			for (int i = 0; i < nodes.size(); i++) {
				Node3D *n = Object::cast_to<Node3D>(Object::cast_to<Node>(nodes[i]));
				if (!n) continue;
				uint64_t id = n->get_instance_id();
				Scaled *rec = nullptr;
				for (auto &s : scaled_) if (s.id == id) { rec = &s; break; }
				const double f = r_now / dressed_radius(a, n, r_orig);
				if (!rec && std::abs(f - 1.0) < 1e-4) continue;
				if (!rec) { scaled_.push_back({ id, a->call("node_get_scale", kMod, "ball.scale", n), true }); rec = &scaled_.back(); }
				Vector3 want = rec->orig * f;
				if (!Vector3(a->call("node_get_scale", kMod, "ball.scale", n)).is_equal_approx(want)) a->call("node_set_scale", kMod, "ball.scale", n, want);
			}
		}
	}


	bool any_player = false;
	for (auto &s : scaled_) if (!s.ball) { any_player = true; break; }
	if (player_scale_ == 1.0 && !any_player) return;
	Array nodes = a->call("find_players", kMod);
	for (int i = 0; i < nodes.size(); i++) {
		Node3D *n = Object::cast_to<Node3D>(Object::cast_to<Node>(nodes[i]));
		if (!n) continue;
		uint64_t id = n->get_instance_id();
		Scaled *rec = nullptr;
		for (auto &s : scaled_) if (s.id == id) { rec = &s; break; }
		if (!rec && player_scale_ == 1.0) continue;
		const Vector3 cur = a->call("node_get_scale", kMod, "player.scale", n);
		if (!rec) { scaled_.push_back({ id, cur, false }); rec = &scaled_.back(); }
		const Vector3 want = rec->orig * player_scale_;
		if (!cur.is_equal_approx(want)) {
			if (player_scale_ == 1.0) a->call("node_restore_scale", kMod, "player.scale", n, want);
			else a->call("node_set_scale", kMod, "player.scale", n, want);
		}
	}
}

void JVGoofyMods::revert_scales() {


	const double r = phys_orig_.has("radius") ? double(phys_orig_["radius"]) : 0.0;
	Object *a = api();
	if (a) for (auto &s : scaled_) if (Node3D *n = Object::cast_to<Node3D>(ObjectDB::get_instance(s.id))) {
		const bool ball = bool(a->call("is_ball", kMod, n));
		Vector3 want = s.orig;
		if (r > 0.0 && ball) want = s.orig * (r / dressed_radius(a, n, r));
		a->call("node_restore_scale", kMod, ball ? "ball.scale" : "player.scale", n, want);
	}
	scaled_.clear();
	player_scale_ = 1.0;
}




Object *JVGoofyMods::rally_for(Node *player) {
	Object *a = api();
	return a ? a->call("get_rally_for", kMod, player).operator Object *() : nullptr;
}

bool JVGoofyMods::enforcing() {
	if (!follow_.applied) return false;
	if (gs_registered_ ? rules_stock() : (max_hits_ == 0 && !no_double_)) return false;


	Object *a = api();
	if (!a) return false;
	Dictionary net = a->call("get_net_state", kMod);
	if (bool(net["found"]) && bool(net["active"]) && !bool(net["is_host"])) return false;
	return true;
}

void JVGoofyMods::hook_players() {
	Object *a = api();
	if (!a) return;
	Array ps = a->call("find_players", kMod);
	for (int i = 0; i < ps.size(); i++) {
		Node *p = Object::cast_to<Node>(ps[i]);
		if (!p || touch_hooked_.count(p->get_instance_id())) continue;

		if (!bool(a->call("connect_player_touched", kMod, p, Callable(this, "_on_player_touched").bind(p)))) continue;
		touch_hooked_.insert(p->get_instance_id());
		if (Object *r = rally_for(p)) rallies_.insert(r->get_instance_id());
	}
	for (auto it = touch_hooked_.begin(); it != touch_hooked_.end();) {
		if (!ObjectDB::get_instance(*it)) it = touch_hooked_.erase(it); else ++it;
	}
}

void JVGoofyMods::_on_player_touched(int64_t, Object *who) {
	if (!enforcing()) return;
	Object *r = rally_for(Object::cast_to<Node>(who));
	if (r) rallies_.insert(r->get_instance_id());
	Object *a = api();
	if (!r || !a) return;
	if (gs_registered_) {
		Dictionary gs = a->call("game_rules_state", kMod, r);
		if (gs.is_empty() || bool(gs["practice"])) return;
		if (r->get_instance_id() != streak_rally_) { streak_ = jvm::rules::Streak(); streak_rally_ = r->get_instance_id(); }
		const int count = int(gs["count"]), side = int(gs["side"]);
		jvm::rules::streak_after_touch(streak_, who ? who->get_instance_id() : 0, side, count);
		if (bool(gs["has_last_toucher"]) && uint64_t(int64_t(gs["last_toucher_id"])) == streak_.who &&
				jvm::rules::may_touch_again(streak_, rule_foul_, rule_consec_) && bool(a->call("rally_clear_last_toucher", kMod, r)))
			consec_clears_++;
		int w = jvm::rules::after_touch(shadow_, side, count, max_hits_);
		if (w >= 0 && w != count && bool(a->call("rally_set_touch_count", kMod, r, w))) rule_writes_++;
		return;
	}
	Dictionary st = a->call("rally_touch_state", kMod, r);
	if (st.is_empty() || bool(st["practice"])) return;
	if (no_double_ && bool(st["has_last_toucher"])) {

		if (bool(a->call("rally_clear_last_toucher", kMod, r))) double_clears_++;
	}
	int game_count = int(st["count"]);
	int w = jvm::rules::after_touch(shadow_, int(st["side"]), game_count, max_hits_);
	if (w >= 0 && w != game_count && bool(a->call("rally_set_touch_count", kMod, r, w))) rule_writes_++;
}

void JVGoofyMods::rules_tick() {
	if (!enforcing()) return;
	Object *a = api();
	if (!a) return;
	if (gs_registered_) {
		if (!streak_rally_ || !jvm::rules::may_touch_again(streak_, rule_foul_, rule_consec_)) return;
		Object *r = ObjectDB::get_instance(streak_rally_);
		if (!r) { streak_rally_ = 0; return; }
		Dictionary gs = a->call("game_rules_state", kMod, r);
		if (gs.is_empty() || bool(gs["practice"])) return;
		if (bool(gs["has_last_toucher"]) && uint64_t(int64_t(gs["last_toucher_id"])) == streak_.who && bool(a->call("rally_clear_last_toucher", kMod, r))) consec_clears_++;
		return;
	}
	if (!no_double_) return;
	for (auto it = rallies_.begin(); it != rallies_.end();) {
		Object *r = ObjectDB::get_instance(*it);
		if (!r) { it = rallies_.erase(it); continue; }
		++it;
		Dictionary st = a->call("rally_touch_state", kMod, r);
		if (st.is_empty() || bool(st["practice"])) continue;
		if (bool(st["has_last_toucher"]) && bool(a->call("rally_clear_last_toucher", kMod, r))) double_clears_++;
	}
}


void JVGoofyMods::sync_tick() {
	Object *a = api();
	if (!a) return;
	Dictionary net = a->call("get_net_state", kMod);
	if (!bool(net["found"]) || !bool(net["steam"])) return;
	if (int64_t(net["lobby_id"]) == 0 || !bool(net["active"])) return;
	if (!bool(a->call("is_open", "sync"))) {
		if (!published_rules_.is_empty() && bool(net["is_host"])) { a->call("lobby_data_clear", kMod, "jvm_rules"); published_rules_ = ""; }
		return;
	}
	if (bool(net["is_host"])) {
		String t = rules_text(max_hits_, no_double_);
		if (t != published_rules_) { a->call("lobby_data_set", kMod, "jvm_rules", t); published_rules_ = t; }
	} else {

		int mh = max_hits_; bool nd = no_double_;
		if (!gs_registered_ && parse_rules_text(String(a->call("lobby_data_get", kMod, "jvm_rules")), mh, nd)) { max_hits_ = mh; no_double_ = nd; }
	}
}




void JVGoofyMods::panel_tick() {
	Object *a = api();
	if (!a) { panel_id_ = 0; return; }
	Node *panel = Object::cast_to<Node>(a->call("modifiers_panel_find", kMod).operator Object *());
	if (!panel) { panel_id_ = 0; panel_dirty_ = false; return; }
	const bool show = follow_.applied && bool(a->call("is_open", "gameplay"));
	const Variant have = a->call("modifiers_panel_rows", kMod, panel);
	Array rows = have.get_type() == Variant::ARRAY ? Array(have) : Array();
	if (!show && have.get_type() == Variant::NIL) return;
	if (panel->get_instance_id() != panel_id_ || rows.is_empty()) {
		if (have.get_type() != Variant::NIL) { panel_id_ = panel->get_instance_id(); return; }
		Array opts; opts.push_back("off");
		for (int i = 1; i <= 10; i++) opts.push_back(String::num_int64(i));
		Dictionary e1; e1["id"] = kMaxHitsId; e1["name"] = "max hits per team"; e1["kind"] = "choice"; e1["default"] = 0; e1["options"] = opts;
		Dictionary e2; e2["id"] = kNoDoubleId; e2["name"] = "no double-hit foul"; e2["kind"] = "toggle"; e2["default"] = false;
		Dictionary d1; d1["entry"] = e1; d1["desc"] = "touches each team may make before the ball must cross. off = normal 3. (jv-mods)";
		Dictionary d2; d2["entry"] = e2; d2["desc"] = "one player may touch the ball twice in a row. still counts toward max hits. (jv-mods)";
		Array defs; defs.push_back(d1); defs.push_back(d2);
		Dictionary res = a->call("modifiers_panel_add_rows", kMod, panel, defs, Callable(this, "_on_panel_changed"));
		panel_id_ = panel->get_instance_id();
		if (!bool(res.get("ok", false))) return;
		rows = res["rows"];
		jvm::log("goofy", "Modifiers panel: added 'max hits per team' + 'no double-hit foul' rows");
	}
	if (!panel_dirty_ && show == last_panel_show_ && max_hits_ == last_panel_hits_ && no_double_ == last_panel_nd_
			&& panel->get_instance_id() == panel_id_ && !rows.is_empty())
		return;
	a->call("modifiers_panel_show_rows", kMod, rows, show);

	Dictionary vals; vals[kMaxHitsId] = max_hits_; vals[kNoDoubleId] = no_double_;
	a->call("modifiers_panel_set_values", kMod, panel, vals);
	last_panel_show_ = show;
	last_panel_hits_ = max_hits_;
	last_panel_nd_ = no_double_;
	panel_dirty_ = false;
}

void JVGoofyMods::_on_panel_changed(String key, Variant value) {
	if (!follow_.applied) return;
	if (gs_registered_) {
		Object *a = api();
		if (!a) return;
		if (key == kMaxHitsId) a->call("gameplay_set", kMod, "goofy.rules.max_team_touches", int(value) <= 0 ? 3 : int(value));
		else if (key == kNoDoubleId) a->call("gameplay_set", kMod, "goofy.rules.double_hit_foul", !bool(value));
		return;
	}
	if (key == kMaxHitsId) set_max_hits(int(value));
	else if (key == kNoDoubleId) set_no_double_hit(bool(value));
}


void JVGoofyMods::follow_sync() {
	if (!match_off_) { follow_.sync(this); return; }
	if (follow_.applied) { follow_.applied = false; revert(); jvm::log("goofy", "off for this match: a player in the lobby does not have Goofy (stock for everyone)"); }
}

bool JVGoofyMods::has_game_event_subscribers() const {
	return !const_cast<JVGoofyMods *>(this)->get_signal_connection_list("game_event").is_empty();
}
bool JVGoofyMods::scales_active() const {
	return std::abs(big_ball_ - 1.0) > 1e-9 || std::abs(player_scale_ - 1.0) > 1e-9 || !scaled_.empty() || pushed_.has("ball.radius");
}
bool JVGoofyMods::ext_active() const {
	return ext_[0] > 0.0 || ext_[2] > 0.0 || ext_[3] > 0.0 || ext_[4] != 1.0;
}
bool JVGoofyMods::pc_tick_needed() const {
	if (pc_in_match_) return true;
	if (sync_role_ == "host" || sync_role_ == "client") return true;
	return !pc_.saved().empty();
}
void JVGoofyMods::refresh_physics_process() {
	const bool rules_on = follow_.applied && !(gs_registered_ ? rules_stock() : (max_hits_ == 0 && !no_double_));
	set_physics_process(ext_active() || rules_on);
}
void JVGoofyMods::mark_work_dirty() {
	live_copy_on_ = pushed_.has("ball.net_half_width") || pushed_.has("ball.antenna_half_width");
	panel_dirty_ = true;
	gate_dirty_ = true;
	refresh_physics_process();
	if (follow_.applied && (enforcing() || has_game_event_subscribers())) {
		hook_players();
		hook_events();
	}
}
void JVGoofyMods::_on_node_added(Node *n) {
	if (!n) return;
	const String name = n->get_name();
	if (name == "ModifiersPanel") panel_dirty_ = true;
}

void JVGoofyMods::_process(double delta) {
	JVM_PROF("goofy.process");




	if (gs_registered_ && (match_t_ -= delta) <= 0.0) {
		match_t_ = 0.5;
		Object *a = api();
		Dictionary ms = a ? Dictionary(a->call("gameplay_match_status", kMod)) : Dictionary();
		const bool off = Dictionary(ms.get("disabled", Dictionary())).has(kMod);
		if (off != match_off_) { match_off_ = off; follow_dirty_ = true; }
		else match_off_ = off;
	}

	follow_poll_ += delta;
	if (follow_dirty_ || follow_poll_ >= 0.5) {
		follow_poll_ = 0.0;
		follow_dirty_ = false;
		follow_sync();
	}
	if (!gs_registered_ && follow_.registered && (gs_try_ -= delta) <= 0.0) { gs_try_ = 2.0; gs_register(); }
	if (panel_dirty_) panel_tick();
	if (!follow_.applied) return;
	if (gate_dirty_ || (gate_poll_ += delta) >= 0.25) {
		gate_poll_ = 0.0;
		gate_dirty_ = false;
		physics_gate_tick();
	}
	rescan_ += delta;
	if (rescan_ >= 0.5) {


		rescan_ = rescan_ >= 1e8 ? 0.17 : -0.17;
		if (override_count_ > 0) if (Object *a = api()) a->call("overrides_apply", kMod);
		if (scales_active()) apply_scales();
		if (live_copy_on_) live_copy_tick();
		if (enforcing() || has_game_event_subscribers()) { hook_players(); hook_events(); }
		if (sync_role_ == "host" || sync_role_ == "client") sync_tick();
	}
	const double ts = gs_registered_ ? gs_time_scale_ : (time_changed_ ? cfg_ts_ : 1.0);
	if (sync_role_ == "host" || sync_role_ == "client" || std::abs(ts - 1.0) > 1e-9) host_sync_tick(delta);
	if (pc_tick_needed()) {
		pc_tick_t_ += delta;
		if (pc_tick_t_ >= 0.1) { pc_tick_t_ = 0.0; pc_tick(); }
	}
}

void JVGoofyMods::_physics_process(double delta) {
	JVM_PROF("goofy.physics");
	ext_tick(delta);
	rules_tick();
}
