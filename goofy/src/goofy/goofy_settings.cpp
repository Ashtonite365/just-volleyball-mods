






#include "goofy_mods.hpp"
#include <cmath>

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>

using namespace godot;

jvm::GameIO &jvm_goofy_io();
static jvm::GameIO &IO() { return jvm_goofy_io(); }
static const char *kMod = "goofy";
static const char *kFeelPath = "res://game/player/player_feel.tres";
static const int kSchema = 2;

static Dictionary fset(const char *id, const char *label, const char *tip, const char *cat, double def, double lo, double hi, double step) {
	Dictionary d;
	d["id"] = id; d["type"] = "float"; d["default"] = def; d["min"] = lo; d["max"] = hi; d["step"] = step;
	d["label"] = label; d["tooltip"] = tip; d["category"] = cat; d["sync"] = "gameplay";
	return d;
}
static Dictionary iset(const char *id, const char *label, const char *tip, int def, int lo, int hi) {
	Dictionary d = fset(id, label, tip, "rules", def, lo, hi, 1);
	d["type"] = "int"; d["default"] = def;
	return d;
}
static Dictionary bset(const char *id, const char *label, const char *tip, bool def) {
	Dictionary d;
	d["id"] = id; d["type"] = "bool"; d["default"] = def; d["label"] = label; d["tooltip"] = tip; d["category"] = "rules"; d["sync"] = "gameplay";
	return d;
}

static Array goofy_defs() {
	Array a;
	a.push_back(fset("goofy.gravity_scale", "ball gravity", "x the game's ball gravity (1 = stock). court packs (space platform) stack on top.", "ball", 1.0, 0.05, 5.0, 0.05));
	a.push_back(fset("goofy.bounce_scale", "bounciness", "x the game's floor bounce (1 = stock).", "ball", 1.0, 0.0, 3.0, 0.05));
	a.push_back(fset("goofy.air_drag", "air drag", "slows the ball in flight, per second (0 = stock). underwater courts add their own.", "ball", 0.0, 0.0, 5.0, 0.05));
	a.push_back(fset("goofy.curve_scale", "spin / curve", "x how much spin curves the ball (1 = stock, 0 = no curve).", "ball", 1.0, 0.0, 5.0, 0.05));
	a.push_back(fset("goofy.ball_size", "ball size (hitbox)", "x the ball's real hitbox; the model follows it (1 = stock).", "ball", 1.0, 0.25, 6.0, 0.05));
	a.push_back(fset("goofy.player_scale", "player size", "x every player's size, visual and collision (1 = stock).", "player", 1.0, 0.25, 4.0, 0.05));
	a.push_back(fset("goofy.time_scale", "game speed", "slow-mo below 1, fast-forward above 1 (1 = stock). in a lobby everyone uses the host's speed.", "match", 1.0, 0.25, 2.0, 0.05));
	a.push_back(bset("goofy.rules.double_hit_foul", "double-hit foul",
			"on (stock): a player may touch the ball at most 'touches in a row' times running (1 = normal volleyball). off: no limit in a row; team touches still apply.", true));
	a.push_back(iset("goofy.rules.max_consecutive_touches", "touches in a row (one player)",
			"how many times running one player may touch while the double-hit foul is on. 1 = stock. blocks never count.", 1, 1, 10));
	a.push_back(iset("goofy.rules.max_team_touches", "team touches",
			"touches a team may make before the ball must cross. one more is a fault: point to the other team (stock 3). blocks never count.", 3, 1, 10));
	a.push_back(bset("goofy.rules.block_classic", "Classic block (no zones, the stuff goes to the cursor)",
			"the game's classic block switch (PlayerFeel.block_classic). off = stock zoned block.", false));


	a.push_back(fset("goofy.topspin_spike_boost", "topspin spike speed (air + forward)",
			"x the speed of a TOPSPIN spike made in the air while holding forward (1 = no boost). regular spikes, sets, dumps and every serve (topspin serves too) stay as they are. in a lobby the host's value is used.",
			"ball", 1.12, 1.0, 1.3, 0.01));
	return a;
}

double JVGoofyMods::gs_num(const char *id, double dflt) {
	Object *a = api();
	if (!a || !gs_registered_) return dflt;
	Variant v = a->call("gameplay_get", kMod, id);
	return v.get_type() == Variant::FLOAT || v.get_type() == Variant::INT ? double(v) : (v.get_type() == Variant::BOOL ? (bool(v) ? 1.0 : 0.0) : dflt);
}
double JVGoofyMods::gs_num_local(const char *id, double dflt) {
	Object *a = api();
	if (!a || !gs_registered_ || !a->has_method("gameplay_info")) return dflt;
	Dictionary info = a->call("gameplay_info", kMod, String(id));
	Variant v = info.get("local", Variant());
	return v.get_type() == Variant::FLOAT || v.get_type() == Variant::INT ? double(v) : (v.get_type() == Variant::BOOL ? (bool(v) ? 1.0 : 0.0) : dflt);
}
bool JVGoofyMods::use_local_gs_layer() const {
	if (sync_role_ == "client") return true;
	if (Object *a = const_cast<JVGoofyMods *>(this)->api()) {
		if (a->has_method("gameplay_role") && String(a->call("gameplay_role", kMod)) == "client") return true;
	}
	return false;
}
bool JVGoofyMods::gs_bool(const char *id, bool dflt) { return gs_num(id, dflt ? 1.0 : 0.0) >= 0.5; }


struct Legacy { const char *key; const char *id; };
static const Legacy kLegacy[] = {
	{ "ball_physics.gravity_scale", "goofy.gravity_scale" }, { "ball_physics.bounce_scale", "goofy.bounce_scale" },
	{ "ball_params.flight.air_drag", "goofy.air_drag" }, { "ball_params.flight.curve_scale", "goofy.curve_scale" },
	{ "big_ball", "goofy.ball_size" }, { "player_scale", "goofy.player_scale" }, { "time_scale", "goofy.time_scale" },
	{ "max_hits_per_team", "goofy.rules.max_team_touches" }, { "no_double_hit", "goofy.rules.double_hit_foul" },
};
static Variant legacy_raw(const Dictionary &cfg, const String &key) {
	if (key.begins_with("ball_physics.")) return Dictionary(cfg.get("ball_physics", Dictionary())).get(key.substr(13), Variant());
	if (key.begins_with("ball_params.")) return Dictionary(cfg.get("ball_params", Dictionary())).get(key.substr(12), Variant());
	return cfg.get(key, Variant());
}

static Variant legacy_value(const String &id, const Variant &raw) {
	const bool num = raw.get_type() == Variant::INT || raw.get_type() == Variant::FLOAT;
	if (id == "goofy.rules.double_hit_foul") return raw.get_type() == Variant::BOOL ? Variant(!bool(raw)) : Variant();
	if (id == "goofy.rules.max_team_touches") return num ? Variant(int64_t(double(raw)) <= 0 ? int64_t(3) : int64_t(double(raw))) : Variant();
	if (num) return double(raw);
	if (raw.get_type() == Variant::DICTIONARY && Dictionary(raw).has("mul")) {
		double m = double(Dictionary(raw)["mul"]);
		if (id == "goofy.curve_scale") return m * 1.0;
		if (id == "goofy.air_drag") return 0.0;
	}
	return Variant();
}
static String legacy_seen_path() { return jvm::mods_dir("goofy").path_join("state").path_join("settings_legacy.json"); }
static Dictionary legacy_snapshot(const Dictionary &cfg) {
	Dictionary seen;
	for (auto &l : kLegacy) seen[l.key] = legacy_raw(cfg, l.key);
	return seen;
}
static void write_seen(const Dictionary &seen) {
	String p = legacy_seen_path();
	DirAccess::make_dir_recursive_absolute(p.get_base_dir());
	Ref<FileAccess> f = FileAccess::open(p, FileAccess::WRITE);
	if (f.is_valid()) { Dictionary d; d["version"] = 1; d["seen"] = seen; f->store_string(JSON::stringify(d, "  ")); }
}


Dictionary JVGoofyMods::_gs_migrate(int64_t from, const Dictionary &values) {
	Dictionary out = values;
	if (from >= kSchema) return out;
	Object *a = api();
	Dictionary cfg = a ? Dictionary(a->call("load_config", kMod, "goofy")) : Dictionary();
	int n = 0;
	for (auto &l : kLegacy) {

		if (from >= 1 && String(l.id) != "goofy.player_scale") continue;
		Variant v = legacy_value(l.id, legacy_raw(cfg, l.key));
		if (v.get_type() == Variant::NIL || out.has(l.id)) continue;
		out[l.id] = v; n++;
	}
	write_seen(legacy_snapshot(cfg));
	jvm::log("goofy", "gameplay settings: imported " + String::num_int64(n) + " value(s) from mods/goofy/*.json (schema " + String::num_int64(from) + " -> " + String::num_int64(kSchema) + "; the files are not changed)");
	return out;
}



void JVGoofyMods::gs_sync_legacy(const Dictionary &cfg) {
	Object *a = api();
	if (!a || !gs_registered_) return;
	Dictionary prev;
	if (FileAccess::file_exists(legacy_seen_path())) { Variant v = jvm::load_json(legacy_seen_path()); if (v.get_type() == Variant::DICTIONARY) prev = Dictionary(v).get("seen", Dictionary()); }
	Dictionary now = legacy_snapshot(cfg);
	for (auto &l : kLegacy) {
		Variant was = prev.get(l.key, Variant()), cur = now.get(l.key, Variant());
		if (!prev.has(l.key) || was == cur) continue;
		Variant v = legacy_value(l.id, cur);
		if (v.get_type() == Variant::NIL) continue;
		Dictionary r = a->call("gameplay_set", kMod, l.id, v);
		jvm::log("goofy", String("config edit of ") + l.key + " -> " + l.id + " = " + String(v) + (bool(r.get("ok", false)) ? String() : " (refused: " + String(r.get("error", "")) + ")"));
	}
	write_seen(now);
}

void JVGoofyMods::gs_register() {
	Object *a = api();
	if (!a || !a->has_method("gameplay_register")) return;
	Dictionary opts;
	opts["version"] = kSchema;
	opts["mod_version"] = "1";
	opts["migrate"] = Callable(this, "_gs_migrate");
	opts["on_change"] = Callable(this, "_on_gs_changed");
	Dictionary r = a->call("gameplay_register", kMod, goofy_defs(), opts);
	Array reg = r.get("registered", Array());
	gs_registered_ = reg.size() == goofy_defs().size();
	if (!gs_registered_) { jvm::warn("goofy", "gameplay settings: registration incomplete (" + JSON::stringify(r.get("errors", Array())) + "); config-file paths stay active"); if (!reg.is_empty()) a->call("gameplay_unregister", kMod); return; }
	jvm::log("goofy", "gameplay settings: " + String::num_int64(reg.size()) + " settings registered (schema " + String::num_int64(kSchema) + ", role " + String(r.get("role", "?")) + ")");
	if (follow_.applied) { reloading_ = true; revert(); apply(); reloading_ = false; }
	else gs_apply();
}

void JVGoofyMods::_on_gs_changed(const String &id, const Variant &, const String &) {
	if (id.begins_with("goofy.")) gs_apply();
}

void JVGoofyMods::gs_apply() {
	if (!gs_registered_) return;
	rule_foul_ = gs_bool("goofy.rules.double_hit_foul", true);
	rule_consec_ = jvm::rules::clamp_consecutive(int(gs_num("goofy.rules.max_consecutive_touches", 1)));
	rule_team_ = jvm::rules::clamp_team_touches(int(gs_num("goofy.rules.max_team_touches", 3)));
	max_hits_ = jvm::rules::team_touches_to_max_hits(rule_team_);
	no_double_ = !rule_foul_;




	if (use_local_gs_layer()) {
		big_ball_ = gs_num_local("goofy.ball_size", 1.0);
		if (!follow_.applied) return;

		apply_block_classic(gs_bool("goofy.rules.block_classic", false));
		if (!host_live_ && !ts_synced_) {
			double ts = gs_num_local("goofy.time_scale", 1.0);
			if (std::abs(ts - 1.0) < 1e-9 && cfg_.has("time_scale") && cfg_["time_scale"].get_type() != Variant::NIL)
				ts = CLAMP(double(cfg_["time_scale"]), 0.1, 4.0);
			apply_time_scale(ts);
			build_user_layer();
			push_library();
		}
		mark_work_dirty();
		return;
	}
	big_ball_ = gs_num("goofy.ball_size", 1.0);
	if (!follow_.applied) { mark_work_dirty(); return; }
	apply_time_scale(gs_num("goofy.time_scale", 1.0));
	apply_block_classic(gs_bool("goofy.rules.block_classic", false));
	build_user_layer();
	push_library();
	mark_work_dirty();
}

void JVGoofyMods::apply_time_scale(double ts) {
	Object *a = api();
	if (!a) return;
	gs_time_scale_ = ts;
	if (std::abs(ts - 1.0) < 1e-9) {
		if (time_changed_) { a->call("time_scale_restore", kMod, orig_time_scale_); time_changed_ = false; jvm::log("goofy", "time_scale -> stock"); }
		return;
	}
	if (!time_changed_) orig_time_scale_ = a->call("time_scale_get");
	if (bool(a->call("time_scale_set", kMod, ts))) { time_changed_ = true; jvm::log("goofy", "time_scale -> " + String::num(ts)); }
}


void JVGoofyMods::apply_block_classic(bool on, bool restore) {
	Ref<Resource> f = feel_.is_valid() ? feel_ : IO().resource("game.rules", kFeelPath);
	if (f.is_null()) return;
	if (!block_classic_set_) {
		if (restore) return;
		Variant cur = IO().get("game.rules", f, "block_classic");
		if (cur.get_type() != Variant::BOOL) return;
		if (bool(cur) == on) return;
		block_classic_orig_ = cur;
		if (IO().set("game.rules", f, "block_classic", on)) { block_classic_set_ = true; jvm::log("goofy", String("block_classic -> ") + (on ? "on" : "off")); }
		return;
	}
	if (restore || on == bool(block_classic_orig_)) {
		IO().set_undo("game.rules", f, "block_classic", block_classic_orig_);
		block_classic_set_ = false;
		jvm::log("goofy", "block_classic -> stock");
		return;
	}
	IO().set("game.rules", f, "block_classic", on);
}

Dictionary JVGoofyMods::get_rules() const {
	Dictionary d;
	d["registry"] = gs_registered_;
	d["double_hit_foul"] = rule_foul_;
	d["max_consecutive_touches"] = rule_consec_;
	d["max_team_touches"] = rule_team_;
	d["stock"] = rules_stock();
	d["block_classic"] = block_classic_set_ ? Variant(!bool(block_classic_orig_)) : Variant(false);
	d["block_classic_written"] = block_classic_set_;
	d["time_scale"] = gs_time_scale_;
	d["consecutive_clears"] = consec_clears_;
	d["count_writes"] = rule_writes_;
	d["streak"] = streak_.count;
	return d;
}
