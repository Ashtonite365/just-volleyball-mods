













#include "goofy_mods.hpp"

#include <cmath>

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/window.hpp>

using namespace godot;
namespace G = jvm::goofy;

static const char *kPhysPath = "res://game/ball/ball_physics.tres";
static const char *kFeelPath = "res://game/player/player_feel.tres";
enum { X_DRAG, X_VDRAG, X_MAX, X_MIN, X_CURVE };
jvm::GameIO &jvm_goofy_io();
static jvm::GameIO &IO() { return jvm_goofy_io(); }
static const char *kMod = "goofy";
static int ext_index(const String &prop) {
	if (prop == "air_drag") return X_DRAG;
	if (prop == "vertical_drag_scale") return X_VDRAG;
	if (prop == "max_speed") return X_MAX;
	if (prop == "min_speed") return X_MIN;
	if (prop == "curve_scale") return X_CURVE;
	return -1;
}


void JVGoofyMods::_ready() {
	load_ball_mode();
	set_physics_process(false);
	if (SceneTree *t = get_tree()) {
		t->connect("node_added", Callable(this, "_on_node_added"));
		hook_watch_ = true;
	}
}


bool JVGoofyMods::open_resources() {
	if (library_loaded_) return phys_.is_valid() || feel_.is_valid();
	library_loaded_ = true;
	String pp = cfg_.get("ball_physics_path", kPhysPath);

	phys_ = IO().resource("ball.physics", pp);
	if (phys_.is_null()) jvm::log("goofy", "ball library: " + pp + " not found (ball.* params skipped)");
	feel_ = IO().resource("player.feel", kFeelPath);
	if (feel_.is_null()) jvm::log("goofy", String("ball library: ") + kFeelPath + " not found (player.* params skipped)");
	return phys_.is_valid() || feel_.is_valid();
}

Variant JVGoofyMods::stock_of(const G::Param &p) {
	String id = p.id;
	if (stock_.has(id)) return stock_[id];
	Variant v;
	if (p.target == G::EXT) v = p.def[0];
	else {
		Ref<Resource> r = p.target == G::PHYS ? phys_ : feel_;
		Dictionary &orig = p.target == G::PHYS ? phys_orig_ : feel_orig_;
		if (r.is_null()) return Variant();
		v = orig.has(p.prop) ? orig[p.prop] : IO().get(p.target == G::PHYS ? "ball.physics" : "player.feel", r, p.prop);
		if (v.get_type() == Variant::NIL) return Variant();
	}
	stock_[id] = v;
	return v;
}


G::ParseResult JVGoofyMods::parse_values(const Dictionary &d, std::map<std::string, G::Value> &out) const {
	G::ParseResult r;
	Array keys = d.keys();
	for (int i = 0; i < keys.size(); i++) {
		String k = keys[i];
		std::string ks = k.utf8().get_data();
		if (k.begins_with("_") || k == "version") continue;
		bool as_mul = false;
		std::string id = G::canonical(ks, &as_mul);
		const G::Param *p = id.empty() ? nullptr : G::find(id);
		if (!p) { r.ignored.push_back(ks); continue; }
		Variant v = d[k];
		G::Value val;
		if (v.get_type() == Variant::DICTIONARY) {
			Dictionary vd = v;
			if (!vd.has("mul") || (vd["mul"].get_type() != Variant::INT && vd["mul"].get_type() != Variant::FLOAT)) { r.errors.push_back(ks + ": object values must be {\"mul\": number}"); continue; }
			if (p->is_color) { r.errors.push_back(ks + ": colours take [r,g,b,a], not mul"); continue; }
			val.mul = true; val.v[0] = double(vd["mul"]);
			if (val.v[0] < 0) { r.errors.push_back(ks + ": mul must be >= 0"); continue; }
		} else if (v.get_type() == Variant::ARRAY || v.get_type() == Variant::COLOR) {
			if (!p->is_color) { r.errors.push_back(ks + " must be a number"); continue; }
			Color c = v.get_type() == Variant::COLOR ? Color(v) : Color();
			if (v.get_type() == Variant::ARRAY) {
				Array a = v;
				if (a.size() < 3) { r.errors.push_back(ks + ": colour needs [r,g,b] or [r,g,b,a]"); continue; }
				c = Color(a[0], a[1], a[2], a.size() > 3 ? double(a[3]) : 1.0);
			}
			val.v[0] = c.r; val.v[1] = c.g; val.v[2] = c.b; val.v[3] = c.a;
		} else if (v.get_type() == Variant::INT || v.get_type() == Variant::FLOAT) {
			if (p->is_color) { r.errors.push_back(ks + ": colours take [r,g,b,a]"); continue; }
			val.mul = as_mul; val.v[0] = double(v);
			if (!as_mul && (val.v[0] < p->min || val.v[0] > p->max))
				r.errors.push_back(ks + " must be between " + String::num(p->min).utf8().get_data() + " and " + String::num(p->max).utf8().get_data() + " (clamped)");
		} else { r.errors.push_back(ks + " must be a number"); continue; }
		out[id] = val;
		r.applied.push_back(id);
	}
	return r;
}

Dictionary JVGoofyMods::result_dict(const G::ParseResult &r) const {
	Dictionary d;
	Array a, ig, er;
	for (auto &s : r.applied) a.push_back(String(s.c_str()));
	for (auto &s : r.ignored) ig.push_back(String(s.c_str()));
	for (auto &s : r.errors) er.push_back(String(s.c_str()));
	d["ok"] = !r.applied.empty() || (r.errors.empty());
	d["applied"] = a; d["ignored"] = ig; d["errors"] = er;
	d["active"] = physics_allowed_;
	return d;
}



void JVGoofyMods::build_user_layer() {
	G::Layer l;
	l.owner = "user"; l.priority = G::kUserPriority;
	Dictionary legacy;


	if (gs_registered_) {
		auto gn = [this](const char *id, double d) { return use_local_gs_layer() ? gs_num_local(id, d) : gs_num(id, d); };
		const double bb = gn("goofy.ball_size", 1.0), gsc = gn("goofy.gravity_scale", 1.0), bsc = gn("goofy.bounce_scale", 1.0);
		if (bb != 1.0) legacy["big_ball"] = bb;
		if (gsc != 1.0) legacy["gravity_scale"] = gsc;
		if (bsc != 1.0) legacy["bounce_scale"] = bsc;
		const double ps = gn("goofy.player_scale", 1.0);
		if (ps != 1.0) legacy["player_scale"] = ps;
	} else if (cfg_.has("big_ball") && cfg_["big_ball"].get_type() != Variant::NIL && double(cfg_["big_ball"]) != 1.0)
		legacy["big_ball"] = CLAMP(double(cfg_["big_ball"]), 0.25, 6.0);
	if (!gs_registered_ && cfg_.has("player_scale") && cfg_["player_scale"].get_type() != Variant::NIL && double(cfg_["player_scale"]) != 1.0)
		legacy["player_scale"] = CLAMP(double(cfg_["player_scale"]), 0.25, 4.0);
	Dictionary bp = cfg_.get("ball_physics", Dictionary());
	Array ks = bp.keys();
	for (int i = 0; i < ks.size(); i++) {
		String k = ks[i];
		if (gs_registered_ && (k == "gravity_scale" || k == "bounce_scale")) continue;
		if ((k == "gravity_scale" || k == "bounce_scale") && double(bp[k]) == 1.0) continue;
		if (k == "gravity_scale") legacy[k] = CLAMP(double(bp[k]), 0.05, 5.0);
		else if (k == "bounce_scale") legacy[k] = CLAMP(double(bp[k]), 0.0, 3.0);
		else legacy[k] = bp[k];
	}
	G::ParseResult r1 = parse_values(legacy, l.values);
	for (auto &k : r1.ignored) jvm::warn("goofy", String("BallPhysics has no property ") + k.c_str());
	G::ParseResult r2 = parse_values(cfg_.get("ball_params", Dictionary()), l.values);
	for (auto &k : r2.ignored) jvm::log("goofy", String("ball_params: ignored unknown key '") + k.c_str() + "'");
	for (auto &e : r2.errors) jvm::warn("goofy", String("ball_params: ") + e.c_str());
	if (gs_registered_) {
		const char *ported[][2] = { { "flight.air_drag", "goofy.air_drag" }, { "flight.curve_scale", "goofy.curve_scale" } };
		for (auto &pp : ported) {
			const G::Param *p = G::find(pp[0]);
			l.values.erase(pp[0]);
			const double v = use_local_gs_layer() ? gs_num_local(pp[1], p ? p->def[0] : 0.0) : gs_num(pp[1], p ? p->def[0] : 0.0);
			if (p && v != p->def[0]) { G::Value gv; gv.v[0] = v; l.values[pp[0]] = gv; }
		}
	}
	G::ParseResult r3 = parse_values(cfg_.get("player_params", Dictionary()), l.values);
	for (auto &k : r3.ignored) jvm::log("goofy", String("player_params: ignored unknown key '") + k.c_str() + "'");
	for (auto &e : r3.errors) jvm::warn("goofy", String("player_params: ") + e.c_str());
	stack_.clear("user");
	stack_.set(l);
	emit_signal("layers_changed");
}


void JVGoofyMods::push_library() {
	if (!physics_allowed_) return;

	if (stack_.layers().empty() && pushed_.is_empty() && (!stack_.host_mode() || stack_.host_values().empty())) return;
	if (!open_resources()) return;
	Array changed;
	for (auto &p : G::params()) {
		Variant sv = stock_of(p);
		if (sv.get_type() == Variant::NIL) continue;
		double st[4] = { 0, 0, 0, 1 }, out[4];
		if (p.is_color) { Color c = sv; st[0] = c.r; st[1] = c.g; st[2] = c.b; st[3] = c.a; }
		else st[0] = double(sv);
		bool differs = stack_.resolve(p, st, out);
		String id = p.id;
		Variant want = p.is_color ? Variant(Color(out[0], out[1], out[2], out[3])) : Variant(out[0]);
		if (!differs) {
			if (!pushed_.has(id)) continue;
			want = sv; pushed_.erase(id);
		} else {
			if (pushed_.has(id) && pushed_[id] == want) continue;
			pushed_[id] = want;
		}
		if (p.target == G::EXT) {
			if (String(p.prop) == "body_scale") player_scale_ = differs ? out[0] : 1.0;
			else if (int x = ext_index(p.prop); x >= 0) ext_[x] = differs ? out[0] : p.def[0];
		} else {
			Ref<Resource> r = p.target == G::PHYS ? phys_ : feel_;
			Dictionary &orig = p.target == G::PHYS ? phys_orig_ : feel_orig_;
			if (differs && !orig.has(p.prop)) orig[p.prop] = sv;
			if (String(p.apply) == "live_int") want = int64_t(std::llround(double(want)));
			const char *cap = p.target == G::PHYS ? "ball.physics" : "player.tuning";
			if (differs) IO().set(cap, r, p.prop, want);
			else IO().set_undo(cap, r, p.prop, want);
			if (!differs) orig.erase(p.prop);
		}
		changed.push_back(id);
	}
	if (!changed.is_empty()) {
		jvm::log("goofy", "ball library: " + String::num_int64(changed.size()) + " param(s) changed, " + String::num_int64(pushed_.size()) +
				" off stock" + (stack_.force_defaults() ? String(" (Ball override FIXED: stock)") : String()) +
				(stack_.host_mode() ? String(" (host values)") : String()));
		host_dirty_ = true;
		live_copy_tick();
		apply_scales();
		emit_signal("params_changed", changed, get_params());
	}
	mark_work_dirty();
}

void JVGoofyMods::restore_library() {
	Array changed = pushed_.keys();
	if (phys_.is_valid()) { Array k = phys_orig_.keys(); for (int i = 0; i < k.size(); i++) IO().set_undo("ball.physics", phys_, k[i], phys_orig_[k[i]]); }
	if (feel_.is_valid()) { Array k = feel_orig_.keys(); for (int i = 0; i < k.size(); i++) IO().set_undo("player.tuning", feel_, k[i], feel_orig_[k[i]]); }
	for (int i = 0; i < 5; i++) ext_[i] = (i == X_VDRAG || i == X_CURVE) ? 1.0 : 0.0;
	player_scale_ = 1.0;
	live_copy_tick(true);
	apply_scales();
	phys_orig_.clear(); feel_orig_.clear(); pushed_.clear();
	if (!changed.is_empty()) { jvm::log("goofy", "ball library: restored to stock"); emit_signal("params_changed", changed, Dictionary()); }
	mark_work_dirty();
}






void JVGoofyMods::physics_gate_tick() {
	bool want = false;
	String role = "off";
	Object *a = follow_.applied ? api() : nullptr;
	if (a) {
		if (bool(a->call("is_open", "offline"))) { want = true; role = "offline"; }
		else if (bool(a->call("is_open", "sync"))) {
			Dictionary net = a->call("get_net_state", kMod);
			if (bool(net.get("found", false)) && bool(net.get("active", false))) {
				want = true;
				role = bool(net.get("is_host", false)) ? "host" : "client";
			}
		}
	}
	if (role != sync_role_) set_sync_role(role);
	if (want == physics_allowed_) return;
	physics_allowed_ = want;
	jvm::log("goofy", String("ball library ") + (want ? "allowed (" + role + ")" : String("paused (not offline / synced lobby) - game ball physics stock")));
	if (want) push_library(); else restore_library();
	emit_signal("physics_allowed_changed", want);
}

static Array balls_now(Object *a) { return a ? Array(a->call("find_balls", kMod)) : Array(); }


void JVGoofyMods::live_copy_tick(bool undo) {
	if (phys_.is_null()) return;
	Array balls = balls_now(api());
	for (const char *prop : { "net_half_width", "antenna_half_width" }) {
		Variant v = IO().get("ball.physics", phys_, prop);
		for (int i = 0; i < balls.size(); i++) if (Node *b = Object::cast_to<Node>(balls[i]))
			if (IO().get("ball.flight", b, prop) != v) IO().set("ball.flight", b, prop, v, undo);
	}
}





void JVGoofyMods::ext_tick(double delta) {
	if (!physics_allowed_) return;
	const bool drag = ext_[X_DRAG] > 0.0, mx = ext_[X_MAX] > 0.0, mn = ext_[X_MIN] > 0.0, cv = ext_[X_CURVE] != 1.0;
	if (!drag && !mx && !mn && !cv) { balls_seen_.clear(); return; }
	Array balls = balls_now(api());
	double radius = phys_.is_valid() ? double(IO().get("ball.physics", phys_, "radius")) : 0.26;
	auto F = [](Node *b, const char *p) { return IO().get("ball.flight", b, p); };
	for (int i = 0; i < balls.size(); i++) {
		Node3D *b = Object::cast_to<Node3D>(Object::cast_to<Node>(balls[i]));
		if (!b || !b->is_inside_tree()) continue;
		if (bool(F(b, "frozen")) || bool(F(b, "serve_held")) || bool(F(b, "net_puppet")) || bool(F(b, "remote_driven"))) continue;
		if (double(F(b, "hitstop_left")) > 0.0) continue;
		Vector3 v = F(b, "vel");
		if (b->get_global_position().y <= radius + 0.01 && std::abs(v.y) < 1e-4) continue;
		if (cv) {
			int64_t seq = int64_t(F(b, "strike_seq"));
			BallSeen *rec = nullptr;
			for (auto &s : balls_seen_) if (s.id == b->get_instance_id()) { rec = &s; break; }
			if (!rec) { balls_seen_.push_back({ b->get_instance_id(), seq }); }
			else if (rec->strike != seq) { rec->strike = seq; IO().set("ball.flight", b, "curve", double(F(b, "curve")) * ext_[X_CURVE]); }
		}
		Vector3 nv = v;
		if (drag) {
			double k = std::exp(-ext_[X_DRAG] * delta), ky = std::exp(-ext_[X_DRAG] * ext_[X_VDRAG] * delta);
			nv.x *= k; nv.z *= k; nv.y *= ky;
		}
		double sp = nv.length();
		if (mx && sp > ext_[X_MAX]) nv *= ext_[X_MAX] / sp;
		else if (mn && sp > 1e-3 && sp < ext_[X_MIN]) nv *= ext_[X_MIN] / sp;
		if (nv != v) { IO().set("ball.flight", b, "vel", nv); ext_frames_++; }
	}
}


Array JVGoofyMods::get_schema() const { return schema_of(0); }
Array JVGoofyMods::get_ball_schema() const { return schema_of(1); }
Array JVGoofyMods::get_player_schema() const { return schema_of(2); }

Array JVGoofyMods::schema_of(int which) const {
	Array a;
	for (auto &p : G::params()) {
		const bool pl = G::is_player_library(p);
		if ((which == 1 && pl) || (which == 2 && !pl)) continue;
		Dictionary d;
		d["library"] = pl ? "player" : "ball";
		d["is_color"] = p.is_color;
		d["id"] = p.id; d["section"] = p.section;
		d["target"] = p.target == G::PHYS ? "BallPhysics" : (p.target == G::FEEL ? "PlayerFeel" : "goofy");
		d["type"] = p.is_color ? "color" : (String(p.apply) == "live_int" ? "int" : "float");
		d["prop"] = p.prop;
		d["default"] = p.is_color ? Variant(Color(p.def[0], p.def[1], p.def[2], p.def[3])) : Variant(p.def[0]);
		d["min"] = p.min; d["max"] = p.max; d["unit"] = p.unit; d["apply"] = p.apply; d["sync"] = G::is_visual(p) ? "local" : "host";
		a.push_back(d);
	}
	return a;
}

Variant JVGoofyMods::get_stock(const String &id) {
	const G::Param *p = G::find(id.utf8().get_data());
	if (!p) return Variant();
	if (p->target != G::EXT && !open_resources()) return p->def[0];
	return stock_of(*p);
}

Variant JVGoofyMods::get_param(const String &id) {
	const G::Param *p = G::find(id.utf8().get_data());
	if (!p) return Variant();
	if (pushed_.has(id)) return pushed_[id];
	return get_stock(id);
}

Dictionary JVGoofyMods::get_params() {
	Dictionary d = pushed_.duplicate();
	return d;
}

Array JVGoofyMods::get_layers() const {
	Array a;
	for (auto &l : stack_.layers()) {
		Dictionary d, v;
		d["owner"] = String(l.owner.c_str()); d["priority"] = l.priority;
		d["scope"] = l.scope == G::Scope::Stage ? "stage" : "session";
		for (auto &kv : l.values) {
			Variant x = kv.second.mul ? Variant(Dictionary()) : Variant(kv.second.v[0]);
			if (kv.second.mul) { Dictionary m; m["mul"] = kv.second.v[0]; x = m; }
			else if (const G::Param *p = G::find(kv.first); p && p->is_color) x = Color(kv.second.v[0], kv.second.v[1], kv.second.v[2], kv.second.v[3]);
			v[String(kv.first.c_str())] = x;
		}
		d["params"] = v;
		a.push_back(d);
	}
	if (stack_.force_defaults()) { Dictionary f; f["owner"] = "fixed"; f["priority"] = G::kForcePriority; f["scope"] = "loader_map"; f["params"] = "ALL -> stock"; a.push_back(f); }
	return a;
}

Dictionary JVGoofyMods::set_override(const String &owner, const Dictionary &params, int priority, const String &scope) {
	G::Layer l;
	l.owner = owner.utf8().get_data();
	G::ParseResult r;
	if (owner.is_empty() || owner == "user" || owner == "fixed") { r.errors.push_back("owner must be a non-empty mod name (not user/fixed)"); return result_dict(r); }
	if (owner.begins_with("pack:")) { r.errors.push_back("pack:<id> owners are set through apply_pack_params"); return result_dict(r); }
	l.priority = CLAMP(priority, 0, G::kMaxModPriority);
	if (priority != l.priority) r.errors.push_back("priority clamped to 0..899");
	l.scope = scope == "stage" ? G::Scope::Stage : G::Scope::Session;
	G::ParseResult p = parse_values(params, l.values);
	p.errors.insert(p.errors.begin(), r.errors.begin(), r.errors.end());
	for (auto &k : p.ignored) jvm::log("goofy", "override " + owner + ": ignored unknown key '" + k.c_str() + "'");
	stack_.set(l);
	jvm::log("goofy", "override " + owner + " (priority " + String::num_int64(l.priority) + ", " + scope + "): " + String::num_int64(l.values.size()) + " param(s)");
	emit_signal("layers_changed");
	push_library();
	mark_work_dirty();
	return result_dict(p);
}

bool JVGoofyMods::clear_override(const String &owner) {
	if (owner == "user") return false;
	bool had = stack_.clear(owner.utf8().get_data());
	if (had) { jvm::log("goofy", "override " + owner + " cleared"); emit_signal("layers_changed"); push_library(); mark_work_dirty(); }
	return had;
}

Dictionary JVGoofyMods::apply_pack_params(const String &pack_id, const Dictionary &params, int version) {
	set_loader_map(pack_id);
	G::Layer l;
	l.owner = ("pack:" + pack_id).utf8().get_data();
	l.priority = G::kPackPriority;
	l.scope = G::Scope::Stage;
	l.stage = pack_id.utf8().get_data();
	if (version > 1) jvm::log("goofy", "pack " + pack_id + ": goofy block version " + String::num_int64(version) + " > 1, applying the keys this build knows");
	G::ParseResult r = parse_values(params, l.values);
	for (auto &k : r.ignored) jvm::log("goofy", "pack " + pack_id + ": ignored unknown key '" + k.c_str() + "'");
	for (auto &e : r.errors) jvm::warn("goofy", "pack " + pack_id + ": " + e.c_str());
	stack_.set(l);
	jvm::log("goofy", "pack " + pack_id + ": " + String::num_int64(l.values.size()) + " court param(s)" + (physics_allowed_ ? "" : " (kept; applies when offline)"));
	emit_signal("layers_changed");
	push_library();
	mark_work_dirty();
	return result_dict(r);
}

bool JVGoofyMods::clear_pack_params(const String &pack_id) {
	bool had = stack_.clear(("pack:" + pack_id).utf8().get_data());
	if (had) { jvm::log("goofy", "pack " + pack_id + ": court params cleared"); emit_signal("layers_changed"); push_library(); }
	return had;
}

void JVGoofyMods::set_loader_map(const String &pack_id) {
	std::string id = pack_id.utf8().get_data();
	if (id == stack_.loader_map()) return;
	bool before = stack_.force_defaults();
	auto dropped = stack_.set_loader_map(id);
	jvm::log("goofy", "loader map -> " + (pack_id.is_empty() ? String("(stock map)") : pack_id) + (dropped.empty() ? "" : ", dropped " + String::num_int64(dropped.size()) + " stage layer(s)"));
	emit_signal("layers_changed");
	if (before != stack_.force_defaults()) emit_signal("ball_mode_changed", ball_mode_, stack_.force_defaults());
	push_library();
}




static String ball_mode_path() { return jvm::mods_dir("goofy").path_join("state").path_join("ball_override.json"); }

void JVGoofyMods::load_ball_mode() {
	String path = ball_mode_path();
	if (!FileAccess::file_exists(path)) return;
	Variant v = jvm::load_json(path);
	if (v.get_type() != Variant::DICTIONARY) return;
	String m = Dictionary(v).get("mode", "normal");
	ball_mode_ = m == "fixed" ? "fixed" : "normal";
	stack_.set_fixed(ball_mode_ == "fixed");
	jvm::log("goofy", "Ball override: " + ball_mode_ + " (" + path + ")");
}

bool JVGoofyMods::save_ball_mode() {
	String path = ball_mode_path();
	DirAccess::make_dir_recursive_absolute(path.get_base_dir());
	String tmp = path + ".tmp";
	{
		Ref<FileAccess> f = FileAccess::open(tmp, FileAccess::WRITE);
		if (f.is_null()) { jvm::warn("goofy", "cannot write " + tmp); return false; }
		Dictionary d; d["version"] = 1; d["mode"] = ball_mode_;
		d["_comment"] = "Ball override on loader (custom pack) maps: normal = map + your Goofy settings, fixed = the game's stock ball everywhere on those maps. Written by the stage picker.";
		f->store_string(JSON::stringify(d, "  "));
	}
	Ref<DirAccess> dir = DirAccess::open(path.get_base_dir());
	if (dir.is_null()) return false;
	if (FileAccess::file_exists(path)) dir->remove(path.get_file());
	return dir->rename(tmp.get_file(), path.get_file()) == OK;
}

bool JVGoofyMods::set_ball_mode(const String &mode) {
	String m = mode.to_lower();
	if (m != "normal" && m != "fixed") return false;
	if (m == ball_mode_) return true;
	ball_mode_ = m;
	stack_.set_fixed(m == "fixed");
	bool ok = save_ball_mode();
	jvm::log("goofy", "Ball override -> " + m + (stack_.force_defaults() ? " (stock ball on this map)" : (stack_.loader_map().empty() ? " (not consulted: stock map)" : "")));
	emit_signal("ball_mode_changed", ball_mode_, stack_.force_defaults());
	push_library();
	return ok;
}


int64_t JVGoofyMods::index_of(const Array &list, Object *o) const {
	if (!o) return -1;
	for (int i = 0; i < list.size(); i++) if (Object *x = list[i]; x == o) return i;
	return -1;
}
Node *JVGoofyMods::ball_at(int64_t index) {
	Array b = balls_now(api());
	return index >= 0 && index < b.size() ? Object::cast_to<Node>(b[index]) : nullptr;
}
Node *JVGoofyMods::player_at(int64_t index) {
	Object *a = api();
	Array p = a ? Array(a->call("find_players", kMod)) : Array();
	return index >= 0 && index < p.size() ? Object::cast_to<Node>(p[index]) : nullptr;
}

Array JVGoofyMods::list_balls() {
	Array out;
	const int64_t n = balls_now(api()).size();
	for (int64_t i = 0; i < n; i++) out.push_back(get_ball_state(i));
	return out;
}



Dictionary JVGoofyMods::get_ball_state(int64_t index) {
	Dictionary d;
	Node3D *b = Object::cast_to<Node3D>(ball_at(index));
	if (!b) return d;
	auto F = [b](const char *p) { return IO().get("ball.flight", b, p); };
	d["index"] = index;
	d["position"] = b->is_inside_tree() ? b->get_global_position() : b->get_position();
	Vector3 v = F("vel");
	d["velocity"] = v;
	d["speed"] = v.length();
	d["curve"] = F("curve");
	d["dive_g"] = F("dive_g");
	d["strike_seq"] = F("strike_seq");
	for (const char *k : { "frozen", "serve_held", "net_puppet", "remote_driven", "float_serve" }) d[k] = bool(F(k));
	d["hitstop_left"] = F("hitstop_left");
	Object *a = api();
	Object *hb = F("hit_by");
	d["hit_by"] = a ? index_of(Array(a->call("find_players", kMod)), hb) : int64_t(-1);
	const double r = phys_.is_valid() ? double(IO().get("ball.physics", phys_, "radius")) : 0.26;
	d["radius"] = r;
	d["scale"] = a ? a->call("node_get_scale", kMod, "ball.scale", b) : Variant();
	const Vector3 pos = d["position"];
	d["airborne"] = !(pos.y <= r + 0.01 && std::abs(v.y) < 1e-4);
	return d;
}


static bool live_ok(bool allowed, const String &role) { return allowed && role == "offline"; }
bool JVGoofyMods::set_ball_velocity(int64_t index, const Vector3 &v) {
	Node *b = ball_at(index);
	if (!b || !live_ok(physics_allowed_, sync_role_) || !v.is_finite()) return false;
	return IO().set("ball.flight", b, "vel", v.limit_length(200.0));
}
bool JVGoofyMods::set_ball_curve(int64_t index, double c) {
	Node *b = ball_at(index);
	if (!b || !live_ok(physics_allowed_, sync_role_) || !std::isfinite(c)) return false;
	return IO().set("ball.flight", b, "curve", CLAMP(c, -100.0, 100.0));
}
bool JVGoofyMods::apply_ball_impulse(int64_t index, const Vector3 &dv) {
	Node *b = ball_at(index);
	if (!b || !live_ok(physics_allowed_, sync_role_) || !dv.is_finite()) return false;
	Vector3 v = IO().get("ball.flight", b, "vel");
	return IO().set("ball.flight", b, "vel", (v + dv).limit_length(200.0));
}

Array JVGoofyMods::list_players() {
	Array out;
	Object *a = api();
	const int64_t n = a ? Array(a->call("find_players", kMod)).size() : 0;
	for (int64_t i = 0; i < n; i++) out.push_back(get_player_state(i));
	return out;
}




Dictionary JVGoofyMods::get_player_state(int64_t index) {
	Node *p = player_at(index);
	Object *a = api();
	if (!p || !a) return Dictionary();
	Dictionary d = a->call("player_get_state", kMod, p);
	d["index"] = index;
	d["local"] = Object::cast_to<Object>(a->call("get_local_player", kMod)) == p;
	Variant vel = IO().get("player.input_state", p, "velocity");
	d["velocity"] = vel;
	d["speed"] = vel.get_type() == Variant::VECTOR3 ? Vector3(vel).length() : 0.0;
	for (const char *k : { "sprinting", "stamina", "is_bot", "dive_cooldown" }) d[k] = IO().get("player.input_state", p, k);
	d["scale"] = a->call("node_get_scale", kMod, "player.scale", p);
	d["body_scale"] = player_scale_;
	Dictionary anim;
	Object *rig = IO().get("player.input_state", p, "_model");
	if (rig) { Variant s = IO().call("player.input_state", rig, "anim_state"); if (s.get_type() == Variant::DICTIONARY) anim = s; }
	d["anim"] = anim;
	return d;
}




Dictionary JVGoofyMods::get_player_cosmetics(int64_t index) {
	Node *p = player_at(index);
	Object *a = api();
	Dictionary d;
	if (!p || !a) return d;
	d["index"] = index;
	Object *ch = IO().get("player.look", p, "character");
	d["has_character"] = ch != nullptr;
	if (ch) {
		d["display_name"] = IO().get("player.look", ch, "display_name");
		d["number"] = IO().get("player.look", ch, "number");
		if (ch->has_method("skin_tone")) d["skin_tone"] = IO().call("player.look", ch, "skin_tone");
		if (ch->has_method("hair_style_name")) d["hair_style"] = IO().call("player.look", ch, "hair_style_name");
		if (ch->has_method("hair_color")) d["hair_color"] = IO().call("player.look", ch, "hair_color");
		Dictionary acc;
		if (ch->has_method("acc_id")) for (const char *slot : { "head", "head2", "face", "extra", "extra2" }) acc[slot] = IO().call("player.look", ch, "acc_id", String(slot));
		d["accessories"] = acc;
		if (ch->has_method("look_dict")) d["look"] = IO().call("player.look", ch, "look_dict");
	}
	const bool local = Object::cast_to<Object>(a->call("get_local_player", kMod)) == p;
	d["local"] = local;
	d["model"] = local ? a->call("model_get_shown", kMod) : Variant("");
	return d;
}


Dictionary JVGoofyMods::set_player_override(const String &owner, const Dictionary &params, int priority, const String &scope) {
	Dictionary keep;
	Array refused;
	Array k = params.keys();
	for (int i = 0; i < k.size(); i++) {
		String key = k[i];
		std::string id = G::canonical(key.utf8().get_data(), nullptr);
		const G::Param *p = id.empty() ? nullptr : G::find(id);
		if (p && !G::is_player_library(*p)) { refused.push_back(key + ": ball library param (set_override)"); continue; }
		keep[key] = params[key];
	}
	Dictionary r = set_override(owner, keep, priority, scope);
	Array er = r.get("errors", Array());
	er.append_array(refused);
	r["errors"] = er;
	return r;
}





static int ev_arity(const char *n) {
	static const std::pair<const char *, int> t[] = {
		{ "bounced", 0 }, { "struck", 3 }, { "net_hit", 0 }, { "wall_hit", 0 }, { "antenna_hit", 1 },
		{ "touched", 1 }, { "swung", 1 }, { "thumped", 2 }, { "knock_started", 2 }, { "touch_attempted", 0 },
		{ "out_of_range", 0 }, { "launched", 2 }, { "jumped", 1 }, { "callout_made", 1 }, { "high_fived", 1 },
		{ "block_broken", 1 }, { "dove_into_net", 0 }, { "dove_into_net_pole", 0 },
	};
	for (auto &e : t) if (String(e.first) == n) return e.second;
	return -1;
}
void JVGoofyMods::hook_events() {
	Object *a = api();
	if (!a) return;
	static const char *kBall[] = { "bounced", "struck", "net_hit", "wall_hit", "antenna_hit" };
	static const char *kPlayer[] = { "touched", "swung", "thumped", "knock_started", "touch_attempted", "out_of_range", "launched",
		"jumped", "callout_made", "high_fived", "block_broken", "dove_into_net", "dove_into_net_pole" };
	for (int kind = 0; kind < 2; kind++) {
		Array list = kind == 0 ? balls_now(a) : Array(a->call("find_players", kMod));
		for (int i = 0; i < list.size(); i++) {
			Node *n = Object::cast_to<Node>(list[i]);
			if (!n || ev_hooked_.count(n->get_instance_id())) continue;
			ev_hooked_.insert(n->get_instance_id());
			const int64_t id = int64_t(n->get_instance_id());
			auto hook = [&](const char *sig) {
				const int ar = ev_arity(sig);
				if (ar < 0 || !n->has_signal(sig)) return;
				static const char *m[] = { "_on_ev0", "_on_ev1", "_on_ev2", "_on_ev3" };
				Callable c = Callable(this, m[ar]).bind(kind == 0 ? "ball" : "player", sig, id);
				a->call(kind == 0 ? "connect_ball_event" : "connect_player_event", kMod, n, sig, c);
			};
			if (kind == 0) for (const char *s : kBall) hook(s);
			else for (const char *s : kPlayer) hook(s);
		}
	}
	if (ev_hooked_.size() > 4096) ev_hooked_.clear();
}
void JVGoofyMods::emit_game_event(const String &kind, const String &name, uint64_t id, const Array &args) {
	if (!follow_.applied) return;
	Object *a = api();
	Object *o = ObjectDB::get_instance(id);
	if (!a || !o) return;
	Array list = kind == "ball" ? balls_now(a) : Array(a->call("find_players", kMod));
	emit_signal("game_event", kind, name, index_of(list, o), args);
}
