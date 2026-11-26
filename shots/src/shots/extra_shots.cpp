#include "extra_shots.hpp"
#include "../common/jv_prof.hpp"
#include <godot_cpp/classes/engine.hpp>
#include <cmath>
#include <algorithm>

#include <godot_cpp/classes/canvas_layer.hpp>
#include <godot_cpp/classes/color_rect.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_map.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/label3d.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/quad_mesh.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/rigid_body3d.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;
using jvm::shots::ShotDef;
using jvm::shots::Vec3;




static const char *const kShotsCaps[] = { "gate.mode", "config.files", "scene.current", "scene.balls", "scene.players",
	"ball.flight", "ball.physics", "ball.float", "ball.solver", "ball.landing_marker", "ball.struck",
	"player.input_state", "player.input", "player.feel",
	"gameplay.settings" };
static jvm::GameIO &IO() {
	static jvm::GameIO io = [] {
		jvm::GameIO g;
		g.mod = "shots";
		g.api_version = "1.3";
		for (const char *c : kShotsCaps) g.caps.push_back(c);
		return g;
	}();
	return io;
}
JVExtraShots::JVExtraShots() {
	follow_.mod = "shots";
	follow_.access = "gameplay";
	follow_.api_version = "1.3";
	for (const char *c : kShotsCaps) follow_.caps.push_back(c);
}

void JVExtraShots::_bind_methods() {
	JVM_PROF_TAP_BIND(JVExtraShots);
	ClassDB::bind_method(D_METHOD("reload_config"), &JVExtraShots::reload_config);
	ClassDB::bind_method(D_METHOD("_on_gate_changed", "active", "reason"), &JVExtraShots::_on_gate_changed);
	ClassDB::bind_method(D_METHOD("_on_access_changed", "access", "open", "reason"), &JVExtraShots::_on_access_changed);
	ClassDB::bind_method(D_METHOD("_on_struck", "pos", "power", "kind", "ball"), &JVExtraShots::_on_struck);
	ClassDB::bind_method(D_METHOD("get_last_spin"), &JVExtraShots::get_last_spin);
	ClassDB::bind_method(D_METHOD("get_status"), &JVExtraShots::get_status);
	ClassDB::bind_method(D_METHOD("get_shot_ids"), &JVExtraShots::get_shot_ids);
	ClassDB::bind_method(D_METHOD("validate_config", "config"), &JVExtraShots::validate_config);
	ClassDB::bind_method(D_METHOD("get_armed"), &JVExtraShots::get_armed);
	ClassDB::bind_method(D_METHOD("get_applied_count"), &JVExtraShots::get_applied_count);
	ClassDB::bind_method(D_METHOD("get_skipped_count"), &JVExtraShots::get_skipped_count);
	ClassDB::bind_method(D_METHOD("get_touch_count"), &JVExtraShots::get_touch_count);
	ClassDB::bind_method(D_METHOD("get_last_topspin"), &JVExtraShots::get_last_topspin);
	ClassDB::bind_method(D_METHOD("get_last_event"), &JVExtraShots::get_last_event);
	ClassDB::bind_method(D_METHOD("arm", "id"), &JVExtraShots::arm);
	ClassDB::bind_method(D_METHOD("get_last_curve"), &JVExtraShots::get_last_curve);
	ClassDB::bind_method(D_METHOD("get_range_box"), &JVExtraShots::get_range_box);
	ClassDB::bind_method(D_METHOD("get_spin_display"), &JVExtraShots::get_spin_display);
	ClassDB::bind_method(D_METHOD("get_spin_bar"), &JVExtraShots::get_spin_bar);
	ClassDB::bind_method(D_METHOD("get_last_float"), &JVExtraShots::get_last_float);
	ClassDB::bind_method(D_METHOD("set_debug_visible", "on"), &JVExtraShots::set_debug_visible);
	ClassDB::bind_method(D_METHOD("is_debug_visible"), &JVExtraShots::is_debug_visible);
	ClassDB::bind_method(D_METHOD("get_debug_text"), &JVExtraShots::get_debug_text);
	ClassDB::bind_method(D_METHOD("get_debug_events"), &JVExtraShots::get_debug_events);
}

double JVExtraShots::now() const { return Time::get_singleton()->get_ticks_msec() / 1000.0; }

PackedStringArray JVExtraShots::get_shot_ids() const {
	PackedStringArray a;
	for (auto &s : shots_) a.push_back(String(s.id.c_str()));
	return a;
}

Dictionary JVExtraShots::get_status() const {
	Dictionary d = follow_.status();
	d["backend"] = backend_;
	d["armed"] = get_armed();
	d["balls_hooked"] = int(hooked_.size());
	d["applied"] = applied_count_;
	d["skipped"] = skipped_count_;
	d["curveball_max_lateral"] = spin_.max_lateral;
	d["curveball_range_box"] = spin_.range_box;
	d["curveball_k"] = Vector2(spin_.k_min, spin_.k_max);
	d["curveball_even_bend"] = spin_.even_bend;
	d["curve_display_remote"] = curve_display_remote_;
	d["spin_display"] = spin_display_;
	if (!last_event_.is_empty()) d["last"] = last_event_;
	return d;
}

static bool is_num(const Variant &v) { return v.get_type() == Variant::INT || v.get_type() == Variant::FLOAT; }
static String range_err(const Dictionary &d, const String &k, double lo, double hi, const String &where = "") {
	if (!d.has(k)) return "";
	if (!is_num(d[k])) return where + k + " must be a number";
	double v = d[k];
	if (v < lo || v > hi) return where + k + " must be between " + String::num(lo) + " and " + String::num(hi) + " (got " + String::num(v) + ")";
	return "";
}

static Key key_from_name(const String &name);
String JVExtraShots::validate_config(const Dictionary &d) const {
	String e;
	if (d.has("backend") && String(d["backend"]) != "jv_ball" && String(d["backend"]) != "rigid") return "backend must be \"jv_ball\" or \"rigid\"";
	if (!(e = range_err(d, "arm_window_s", 0.2, 10.0)).is_empty()) return e;
	if (!(e = range_err(d, "curve_amount", 0.0, 60.0)).is_empty()) return e;
	if (d.has("curve_display_remote")) {
		if (d["curve_display_remote"].get_type() != Variant::STRING) return "curve_display_remote must be grey, full or off";
		String v = String(d["curve_display_remote"]).to_lower();
		if (v != "grey" && v != "gray" && v != "full" && v != "off") return "curve_display_remote must be grey, full or off";
	}
	if (d.has("spin_display")) {
		if (d["spin_display"].get_type() != Variant::STRING) return "spin_display must be question or exact";
		String v = String(d["spin_display"]).to_lower();
		if (v != "question" && v != "exact") return "spin_display must be question or exact";
	}
	if (d.has("debugger")) {
		if (d["debugger"].get_type() != Variant::DICTIONARY) return "debugger must be an object {\"key\": \"KEY_F7\", \"events\": 8, \"log\": true, \"start_visible\": false}";
		Dictionary dc = d["debugger"];
		if (dc.has("key") && (dc["key"].get_type() != Variant::STRING || key_from_name(String(dc["key"])) == KEY_NONE)) return "debugger.key must be a key name like \"KEY_F7\"";
		if (!(e = range_err(dc, "events", 1, 30, "debugger.")).is_empty()) return e;
	}
	for (const char *sec : { "curveball", "side_spin" }) {
		if (!d.has(sec)) continue;
		if (d[sec].get_type() != Variant::DICTIONARY) return String(sec) + " must be an object { ... }";
		Dictionary sp = d[sec];
		const struct { const char *k; double lo, hi; } R[] = { { "max_lateral", 0, 20 }, { "spread_long", 0, 10 }, { "spread_short", 0, 10 },
			{ "quality_spread", 0, 10 }, { "extreme_spread", 0, 10 }, { "extreme_from", 0, 0.99 }, { "curve_strength", 0, 60 },
			{ "bar_forward", 0, 10 }, { "k_min", 0, 10 }, { "k_max", 0, 10 } };
		for (auto &r : R) if (!(e = range_err(sp, r.k, r.lo, r.hi, String(sec) + ".")).is_empty()) return e;
		if (sp.has("range_box") && sp["range_box"].get_type() != Variant::BOOL) return String(sec) + ".range_box must be true/false";
		if (sp.has("even_bend") && sp["even_bend"].get_type() != Variant::BOOL) return String(sec) + ".even_bend must be true/false";
	}
	if (!d.has("shots")) return "";
	if (d["shots"].get_type() != Variant::DICTIONARY) return "shots must be an object { ... }";
	Dictionary over = d["shots"];
	std::vector<jvm::shots::ShotDef> defs = jvm::shots::default_shots();
	Array ids = over.keys();
	for (int i = 0; i < ids.size(); i++) {
		String id = ids[i];
		if (id.begins_with("_")) continue;
		const jvm::shots::ShotDef *def = nullptr;
		const String want = id == "side_spin" ? String("curveball") : id;
		for (auto &x : defs) if (want == String(x.id.c_str())) def = &x;
		if (!def) return "unknown shot \"" + id + "\"";
		if (over[id].get_type() != Variant::DICTIONARY) return id + " must be an object { ... }";
		Dictionary o = over[id];
		if (!(e = range_err(o, "speed", 0.0, 80.0, id + ".")).is_empty()) return e;
		jvm::shots::Combo k = def->combo;
		Dictionary c = o.get("combo", Dictionary());
		if (o.has("trigger")) c["trigger"] = o["trigger"];
		auto pick = [&](const char *key, std::initializer_list<const char *> ok, std::string &dst) -> String {
			if (!c.has(key)) return "";
			String v = c[key];
			for (const char *x : ok) if (v == x) { dst = v.utf8().get_data(); return ""; }
			String list; for (const char *x : ok) list += String(list.is_empty() ? "" : "|") + x;
			return id + ".combo." + key + " must be " + list;
		};
		if (!(e = pick("trigger", { "touch", "off" }, k.trigger)).is_empty()) return e;
		if (!(e = pick("context", { "rally", "serve" }, k.context)).is_empty()) return e;
		if (!(e = pick("touch", { "any", "set", "not_set" }, k.touch)).is_empty()) return e;
		if (!(e = pick("air", { "any", "ground", "air" }, k.air)).is_empty()) return e;
		if (!(e = range_err(c, "min_power", 0.0, 1.0, id + ".combo.")).is_empty()) return e;
		if (!(e = range_err(c, "max_power", 0.0, 1.0, id + ".combo.")).is_empty()) return e;
		if (c.has("min_power")) k.min_power = c["min_power"];
		if (c.has("max_power")) k.max_power = c["max_power"];
		if (k.min_power > k.max_power) return id + ".combo: min_power is above max_power";
		if (c.has("curve") && c["curve"].get_type() != Variant::BOOL) return id + ".combo.curve must be true/false";
		if (c.has("hold")) {
			if (c["hold"].get_type() != Variant::ARRAY) return id + ".combo.hold must be a list of the game's action names";
			Array h = c["hold"]; k.hold.clear();
			for (int j = 0; j < h.size(); j++) {
				if (h[j].get_type() != Variant::STRING) return id + ".combo.hold must be a list of action names";
				String a = h[j];
				if (a != "side" && a != "back" && !InputMap::get_singleton()->has_action(a)) return id + ".combo.hold: \"" + a + "\" is not one of the game's actions";
				k.hold.push_back(a.utf8().get_data());
			}
		}
		if (k.trigger == "off") continue;
		std::string clash = jvm::shots::combo_game_clash(k);
		if (!clash.empty()) return id + ": " + String(clash.c_str());
	}
	return "";
}

static Key key_from_name(const String &name) {
	int kp = -1;
	if (jvm::shots::parse_key_name(name.utf8().get_data(), kp)) return Key(int(KEY_KP_0) + kp);
	String k = name.trim_prefix("KEY_").replace("_", " ");
	return Key(OS::get_singleton()->find_keycode_from_string(k));
}

void JVExtraShots::apply() {
	cfg_ = jvm::load_category_config("shots");
	backend_ = String(cfg_.get("backend", "jv_ball"));
	arm_window_ = CLAMP(double(cfg_.get("arm_window_s", 2.0)), 0.2, 10.0);
	{
		Dictionary dc = cfg_.get("debugger", Dictionary());
		Key k = key_from_name(String(dc.get("key", "KEY_F7")));
		if (k != KEY_NONE) dbg_key_ = k;
		dbg_n_ = CLAMP(int(dc.get("events", 8)), 1, 30);
		dbg_log_ = bool(dc.get("log", true));
		if (bool(dc.get("start_visible", false)) && !dbg_on_) call_deferred("set_debug_visible", true);
	}
	shots_ = jvm::shots::default_shots();
	Dictionary over = Dictionary(cfg_.get("shots", Dictionary())).duplicate();

	if (over.has("side_spin")) {
		if (!over.has("curveball")) over["curveball"] = over["side_spin"];
		jvm::log("shots", "config: shots.side_spin is the old name of shots.curveball (still read)");
	}
	for (auto &s : shots_) {
		String id = s.id.c_str();
		if (!over.has(id)) continue;
		Dictionary o = over[id];
		s.speed = double(o.get("speed", s.speed));
		s.elevation_deg = double(o.get("elevation_deg", s.elevation_deg));
		s.topspin_rps = double(o.get("topspin_rps", s.topspin_rps));
		s.float_strength = double(o.get("float_strength", s.float_strength));
		s.float_duration_s = double(o.get("float_duration_s", s.float_duration_s));
		s.curve = double(o.get("curve", s.curve));
		s.default_key = String(o.get("key", String(s.default_key.c_str()))).utf8().get_data();

		Dictionary c = o.get("combo", Dictionary());
		if (o.has("trigger")) c["trigger"] = o["trigger"];
		auto &k = s.combo;
		k.trigger = String(c.get("trigger", String(k.trigger.c_str()))).utf8().get_data();
		k.context = String(c.get("context", String(k.context.c_str()))).utf8().get_data();
		k.touch = String(c.get("touch", String(k.touch.c_str()))).utf8().get_data();
		k.air = String(c.get("air", String(k.air.c_str()))).utf8().get_data();
		k.min_power = double(c.get("min_power", k.min_power));
		k.max_power = double(c.get("max_power", k.max_power));
		k.curve = bool(c.get("curve", k.curve));
		if (c.has("hold")) {
			k.hold.clear();
			Array h = c["hold"];
			for (int i = 0; i < h.size(); i++) k.hold.push_back(String(h[i]).utf8().get_data());
		}
		std::string clash = jvm::shots::combo_game_clash(k);
		if (!clash.empty()) jvm::warn("shots", String(s.id.c_str()) + ": combo clashes with the game (" + String(clash.c_str()) + ")");
	}
	{

		Dictionary sp = Dictionary(cfg_.get("side_spin", Dictionary())).duplicate();
		Dictionary cb = cfg_.get("curveball", Dictionary());
		Array ck = cb.keys();
		for (int i = 0; i < ck.size(); i++) sp[ck[i]] = cb[ck[i]];
		jvm::shots::SpinParams d;
		spin_.max_lateral = CLAMP(double(sp.get("max_lateral", d.max_lateral)), 0.0, 20.0);
		spin_.spread_long = CLAMP(double(sp.get("spread_long", d.spread_long)), 0.0, 10.0);
		spin_.spread_short = CLAMP(double(sp.get("spread_short", d.spread_short)), 0.0, 10.0);
		spin_.quality_spread = CLAMP(double(sp.get("quality_spread", d.quality_spread)), 0.0, 10.0);
		spin_.extreme_spread = CLAMP(double(sp.get("extreme_spread", d.extreme_spread)), 0.0, 10.0);
		spin_.extreme_from = CLAMP(double(sp.get("extreme_from", d.extreme_from)), 0.0, 0.99);
		spin_.curve_strength = CLAMP(double(sp.get("curve_strength", d.curve_strength)), 0.0, 60.0);
		spin_.range_box = bool(sp.get("range_box", d.range_box));
		spin_.bar_forward = CLAMP(double(sp.get("bar_forward", d.bar_forward)), 0.0, 10.0);
		spin_.k_min = CLAMP(double(sp.get("k_min", d.k_min)), 0.0, 10.0);
		spin_.k_max = CLAMP(double(sp.get("k_max", d.k_max)), spin_.k_min, 10.0);
		spin_.even_bend = bool(sp.get("even_bend", d.even_bend));
	}
	{
		Dictionary fl = cfg_.get("float", Dictionary());
		float_lateral_ = CLAMP(double(fl.get("side_lateral", 3.0)), 0.0, 10.0);
	}
	{

		String r = String(cfg_.get("curve_display_remote", "grey")).to_lower();
		curve_display_remote_ = (r == "full" || r == "off" || r == "grey" || r == "gray") ? (r == "gray" ? String("grey") : r) : String("grey");
		if (r != curve_display_remote_ && r != "gray") jvm::warn("shots", "curve_display_remote "" + r + "" is not grey|full|off; using grey");
		String s = String(cfg_.get("spin_display", "question")).to_lower();
		spin_display_ = (s == "question" || s == "exact") ? s : String("question");
		if (s != spin_display_) jvm::warn("shots", "spin_display "" + s + "" is not question|exact; using question");
	}
	curve_amount_ = double(cfg_.get("curve_amount", 12.0));
	held_actions_.clear();
	for (auto &s : shots_) for (auto &h : s.combo.hold) {
		String a = h.c_str();
		if (!held_actions_.has(a)) held_actions_.push_back(a);
	}
	for (const char *a : { "move_left", "move_right" }) if (!held_actions_.has(a)) held_actions_.push_back(a);
	InputMap *im = InputMap::get_singleton();
	for (int i = 0; i < actions_.size(); i++) if (im->has_action(actions_[i])) im->erase_action(actions_[i]);
	actions_.clear();
	if (backend_ == "rigid") {
		for (auto &s : shots_) {
			String action = String("jvm_shot_") + s.id.c_str();
			if (im->has_action(action)) im->erase_action(action);
			im->add_action(action);
			Key code = key_from_name(String(s.default_key.c_str()));
			if (code != KEY_NONE) {
				Ref<InputEventKey> ev; ev.instantiate();
				ev->set_physical_keycode(code);
				im->action_add_event(action, ev);
			}
			actions_.push_back(action);
		}
	}
	armed_ = -1;
	scan_t_ = 1e9;
	box_ = RangeBox();

	set_physics_process_priority(1000);
	if (backend_ == "rigid") jvm::log("shots", "registered " + String::num_int64(actions_.size()) + " shot actions, backend rigid");
	else {
		String m;
		for (auto &s : shots_) {
			auto &k = s.combo;
			String hold;
			for (auto &h : k.hold) hold += String(hold.is_empty() ? "" : "+") + h.c_str();
			m += String(" ") + s.id.c_str() + "=" + (k.trigger == "forward_keys" ? String("forward keys") : hold) + "/" + k.context.c_str();
		}
		jvm::log("shots", "backend jv_ball: " + String::num_int64(shots_.size()) + " shots on the game's own actions (no new keys):" + m);
	}
}

void JVExtraShots::revert() {
	InputMap *im = InputMap::get_singleton();
	for (int i = 0; i < actions_.size(); i++) if (im->has_action(actions_[i])) im->erase_action(actions_[i]);
	actions_.clear();
	float_shot_ = nullptr;
	armed_ = -1;
	clear_spin_display("shots off");

	jvm::log("shots", "shots off");
}

void JVExtraShots::arm(const String &id) {
	for (int i = 0; i < int(shots_.size()); i++) {
		if (String(shots_[i].id.c_str()) != id) continue;
		armed_ = i; armed_at_ = now();
		last_event_ = "armed " + id;
		jvm::log("shots", "armed " + id + " for your next " + (shots_[i].is_serve ? "serve" : "touch"));
		return;
	}
}

Node *JVExtraShots::local_player() {
	Variant spec = cfg_.get("local_player", Variant());
	if (spec.get_type() != Variant::DICTIONARY) { Dictionary d; d["scene_property"] = "player"; spec = d; }
	TypedArray<Node> p = jvm::find_targets(this, spec);
	return p.is_empty() ? nullptr : Object::cast_to<Node>(p[0]);
}



Node *JVExtraShots::local_player_for(Node *ball) {
	if (Object *o = jvm::owner_property(ball, "player", "Player")) return Object::cast_to<Node>(o);
	return local_player();
}

void JVExtraShots::hook_ball(Node *ball) {
	const uint64_t id = ball->get_instance_id();
	if (hooked_.count(id) || !ball->has_signal("struck")) return;
	Callable mine = Callable(this, "_on_struck").bind(ball);


	TypedArray<Dictionary> conns = ball->get_signal_connection_list("struck");
	std::vector<std::pair<Callable, int>> others;
	for (int i = 0; i < conns.size(); i++) {
		Dictionary c = conns[i];
		Callable cb = c["callable"];
		others.push_back({ cb, int(c.get("flags", 0)) });
	}
	bool reordered = true;
	for (auto &o : others) IO().disconnect("ball.struck", ball, "struck", o.first);
	IO().connect("ball.struck", ball, "struck", mine);
	for (auto &o : others) if (IO().connect("ball.struck", ball, "struck", o.first, o.second) != OK) reordered = false;
	hooked_.insert(id);
	jvm::log("shots", "hooked ball " + String(ball->get_path()) + " (" + String::num_int64(others.size()) + " game handler(s) kept" + (reordered ? ", ours first)" : ", REORDER FAILED)"));
}

void JVExtraShots::scan_balls() {
	Variant spec = cfg_.get("ball", Variant());
	if (spec.get_type() != Variant::DICTIONARY) { Dictionary d; d["script_class"] = "Ball"; spec = d; }
	TypedArray<Node> balls = jvm::find_targets(this, spec);
	for (int i = 0; i < balls.size(); i++) if (Node *b = Object::cast_to<Node>(balls[i])) hook_ball(b);

	for (auto it = hooked_.begin(); it != hooked_.end();) {
		if (!ObjectDB::get_instance(*it)) it = hooked_.erase(it); else ++it;
	}
}

void JVExtraShots::_process(double) {
	JVM_PROF("shots.process");
	follow_.sync(this);
	if (!is_processing_input()) set_process_input(true);
	if (dbg_on_) dbg_update();
	if (!follow_.applied) return;
	Input *in = Input::get_singleton();
	InputMap *im = InputMap::get_singleton();


	if (backend_ == "rigid") for (auto &s : shots_) {
		String a = String("jvm_shot_") + s.id.c_str();
		if (im->has_action(a) && in->is_action_just_pressed(a)) fire_rigid(s);
	}
	if (backend_ != "jv_ball") return;
	if (armed_ >= 0 && now() - armed_at_ > arm_window_) {
		last_event_ = String("expired ") + shots_[armed_].id.c_str();
		shot_log(last_event_ + " (no touch within " + String::num(arm_window_, 1) + " s)");
		armed_ = -1;
	}
	scan_t_ += get_process_delta_time();
	if (scan_t_ >= 0.5) {

		scan_t_ = scan_t_ >= 1e8 ? 0.33 : -0.33;
		scan_balls();
	}
}

double JVExtraShots::serving_seen_for(Node *me) const {
	if (!me) return -100.0;
	auto it = serving_seen_.find(uint64_t(me->get_instance_id()));
	return it == serving_seen_.end() ? -100.0 : it->second;
}

String JVExtraShots::court_mode() {
	Object *a = IO().api();
	return a ? String(a->call("get_mode")) : String();
}


Camera3D *JVExtraShots::cam_for(Node *me) const {
	if (!me) return nullptr;
	for (Node *n = me->get_parent(); n; n = n->get_parent()) {
		Variant rv = IO().get("player.input", n, "camera_rig");
		if (rv.get_type() != Variant::OBJECT) continue;
		if (Object *rig = rv) if (Camera3D *c = Object::cast_to<Camera3D>(IO().get("player.input", rig, "camera").operator Object *())) return c;
	}
	return me->get_viewport() ? me->get_viewport()->get_camera_3d() : nullptr;
}



jvm::shots::FrameRaw JVExtraShots::frame_of(Node *me, Vector2 raw) const {
	jvm::shots::FrameRaw f;
	f.x = raw.x; f.y = raw.y;
	Camera3D *cam = cam_for(me);
	Node3D *m3 = Object::cast_to<Node3D>(me);
	if (!cam || !m3) return f;
	Vector3 p = m3->get_global_position();
	Variant av = IO().get("player.input_state", me, "aim_point");
	Vector3 aim = av.get_type() == Variant::VECTOR3 ? Vector3(av) : Vector3(NAN, 0, NAN);


	if (bool(IO().get("player.input_state", me, "serving")) || now() - const_cast<JVExtraShots *>(this)->serving_seen_for(me) <= 0.6) aim = Vector3(NAN, 0, NAN);
	Variant ts = IO().get("player.input_state", me, "team_side");
	int side = ts.get_type() == Variant::INT || ts.get_type() == Variant::FLOAT ? int(double(ts)) : 0;
	jvm::shots::Vec3 nd = jvm::shots::hitter_net_dir({ p.x, 0, p.z }, { aim.x, 0, aim.z }, side);
	Basis b = cam->get_global_transform().basis;
	Vector3 cx = b.get_column(0), cz = b.get_column(2);
	return jvm::shots::hitter_frame_raw({ cx.x, cx.y, cx.z }, { cz.x, cz.y, cz.z }, raw.x, raw.y, nd);
}


jvm::shots::TouchState JVExtraShots::build_touch(Node *me, double power, int64_t kind) {
	Input *in = Input::get_singleton();
	InputMap *im = InputMap::get_singleton();
	jvm::shots::TouchState t;

	t.serving = now() - serving_seen_for(me) <= 0.6;
	t.set_touch = kind == 2;
	t.airborne = me && me->has_method("grounded") ? !bool(IO().call("player.input_state", me, "grounded")) : false;
	t.power = power;
	t.forward_armed = armed_ >= 0 && shots_[armed_].combo.trigger == "forward_keys";


	const bool has_move = im->has_action("move_left") && im->has_action("move_right") && im->has_action("move_forward") && im->has_action("move_back");
	Vector2 raw = has_move ? in->get_vector("move_left", "move_right", "move_forward", "move_back") : Vector2();
	jvm::shots::FrameRaw fr = frame_of(me, raw);
	jvm::shots::MoveIntent mi = jvm::shots::move_intent(fr.x, fr.y);
	for (int i = 0; i < held_actions_.size(); i++) {
		const String &a = held_actions_[i];
		if (a == "move_forward") { if (mi.forward) t.held.push_back("move_forward"); continue; }
		if (a == "move_left" || a == "move_right") continue;

		if (a == "back") { if (mi.back && !mi.forward) t.held.push_back("back"); continue; }
		if (a == "side") { if (mi.side != 0 && !mi.forward && !mi.back) t.held.push_back("side"); continue; }

		if (a == "move_back") { if (mi.back && !mi.forward) t.held.push_back("move_back"); continue; }
		if (im->has_action(a) && in->is_action_pressed(a)) t.held.push_back(a.utf8().get_data());
	}
	return t;
}


int JVExtraShots::decide(Node *me, jvm::shots::TouchState &t, String &why) {
	why = "";
	int idx = jvm::shots::select_shot(shots_, t);
	if (idx >= 0 || !t.serving) return idx;







	jvm::shots::TouchState r = t; r.serving = false;
	int ri = jvm::shots::select_shot(shots_, r);
	if (ri < 0) return -1;
	const bool own_ball = me && bool(IO().get("player.input_state", me, "shared_ball_off"));
	const String mode = court_mode();
	const bool solo_court = mode == "Free Play" || mode == "practice";
	if (solo_court || (own_ball && t.airborne && shots_[ri].combo.air == "air")) {
		t = r;
		return ri;
	}
	why = String("skipped ") + shots_[ri].id.c_str() + ": serve contact in a match (" + mode + "): rally shots wait 0.6 s after a serve; "
			"only the sprint serve shots apply to a serve";
	return -1;
}

void JVExtraShots::_on_struck(Vector3 pos, double power, int64_t kind, Object *ball_obj) {
	if (!follow_.applied || backend_ != "jv_ball") return;
	Node *ball = Object::cast_to<Node>(ball_obj);
	Node *me = ball ? local_player_for(ball) : nullptr;
	if (!ball || !me) return;

	Variant hb = IO().get("ball.flight", ball, "hit_by");
	Object *hitter = hb.get_type() == Variant::OBJECT ? hb.operator Object *() : nullptr;
	if (hitter != me) return;
	touches_++;
	jvm::shots::TouchState t = build_touch(me, power, kind);
	String why;
	const bool in_window = t.serving;
	int idx = decide(me, t, why);
	{

		String held;
		for (auto &h : t.held) held += String(held.is_empty() ? "" : "+") + h.c_str();
		static const char *kinds[] = { "none", "bump", "set", "spike", "dump", "block", "carom" };
		String head = String::num(now(), 2) + "s " + String(kind >= 0 && kind <= 6 ? kinds[kind] : "?") + (t.airborne ? " AIR" : " ground") +
				" power " + String::num(power, 2) + (in_window ? " [serve contact]" : " [rally]") + " held " + (held.is_empty() ? String("-") : held);
		if (idx < 0) dbg_event(head + " -> " + (why.is_empty() ? String("game's own shot (no custom combo held)") : why));
		else dbg_event(head + " -> " + String(shots_[idx].id.c_str()));
	}
	if (idx < 0) {
		if (!why.is_empty()) { skipped_count_++; last_event_ = why; shot_log(last_event_); return; }
		if (armed_ >= 0) last_event_ = String("kept ") + shots_[armed_].id.c_str() + " armed (" + (t.serving ? "not on a serve" : "needs a rally touch") + ")";
		return;
	}
	if (jvm::shots::yields_to_game_dump(shots_[idx], int(kind))) {


		last_event_ = String("kept the game's dump (forward-hold dump, v1.0.4): ") + shots_[idx].id.c_str() + " not applied";
		shot_log(last_event_);
		return;
	}
	InputMap *im = InputMap::get_singleton();
	const bool has_move = im->has_action("move_left") && im->has_action("move_right") && im->has_action("move_forward") && im->has_action("move_back");
	Vector2 raw0 = has_move ? Input::get_singleton()->get_vector("move_left", "move_right", "move_forward", "move_back") : Vector2();
	frame_ = frame_of(me, raw0);
	const Vector2 raw(frame_.x, frame_.y);
	jvm::shots::MoveIntent mi = jvm::shots::move_intent(raw.x, raw.y);
	const ShotDef &s = shots_[idx];
	auto skip = [&](const String &why) {
		skipped_count_++;
		last_event_ = String("skipped ") + s.id.c_str() + ": " + why;
		shot_log(last_event_);
		armed_ = -1;
	};
	if (!bool(IO().get("player.input_state", me, "sim_authority"))) { skip("this machine is not authoritative for your player (online client: the host computes the launch)"); return; }
	if (bool(IO().get("ball.flight", ball, "net_puppet")) || bool(IO().get("ball.flight", ball, "remote_driven"))) { skip("ball is driven by the network"); return; }
	Vector3 bpos = Object::cast_to<Node3D>(ball) ? Object::cast_to<Node3D>(ball)->get_global_position() : pos;
	Vector3 aim = IO().get("player.input_state", me, "aim_point");
	Vector3 f = aim - bpos;
	if (Vector2(f.x, f.z).length() < 0.5) {
		if (Node3D *m3 = Object::cast_to<Node3D>(me)) f = -m3->get_global_transform().basis.get_column(2);
	}
	auto g = jvm::shots::game_launch(s, { f.x, f.y, f.z });

	auto find_cam = [&]() -> Camera3D * {
		for (Node *n = me->get_parent(); n; n = n->get_parent()) {
			Variant rv = IO().get("player.input", n, "camera_rig");
			if (rv.get_type() != Variant::OBJECT) continue;
			if (Object *rig = rv) if (Camera3D *c = Object::cast_to<Camera3D>(IO().get("player.input", rig, "camera").operator Object *())) return c;
		}
		return ball->get_viewport() ? ball->get_viewport()->get_camera_3d() : nullptr;
	};
	if (s.side_spin) { apply_side_spin(ball, me, find_cam(), raw.x, aim, kind, power, s); return; }
	if (s.rally_float) { apply_rally_float(ball, me, aim, kind, power, s, mi.side); return; }
	box_.on = false;
	if (!s.is_serve && s.topspin_rps > 0.0 && t.airborne && ball->has_method("solve_launch") && ball->has_method("_predict_land_curved")) {




		const String bmode = court_mode();
		const bool solo_court = bmode == "Free Play" || bmode == "practice";
		const bool boost_on = jvm::shots::spike_boost_applies(s, in_window && !solo_court, int(kind), t.airborne, mi.forward);
		apply_topspin_cursor(ball, me, find_cam(), raw.x, mi.side, aim, kind, power, s, boost_on ? spike_boost_setting() : 1.0);
		return;
	}


	double curve = g.curve;
	const bool keep_game_curve = !t.airborne;
	if (keep_game_curve) curve = double(IO().get("ball.flight", ball, "curve"));
	int side = 0;
	if (s.combo.curve && t.airborne && mi.side != 0) {
		side = mi.side;



		jvm::shots::Vec3 w = { frame_.net_right.x * raw.x, 0.0, frame_.net_right.z * raw.x };
		curve = jvm::shots::world_curve(w, g.vel, curve_amount_);
	}

	if (ball->has_method("stop_float")) IO().call("ball.float", ball, "stop_float");
	IO().set("ball.flight", ball, "vel", Vector3(g.vel.x, g.vel.y, g.vel.z));
	IO().set("ball.flight", ball, "curve", curve);
	if (g.start_float && ball->has_method("start_float") && ball->has_method("float_seed_for")) {
		int pattern = int(UtilityFunctions::randi() % 1023) + 1;
		Variant seed = IO().call("ball.float", ball, "float_seed_for", pattern, g.float_charge);
		IO().call("ball.float", ball, "start_float", seed);
		IO().set("ball.flight", ball, "curve", curve);
	} else {
		IO().set("ball.flight", ball, "dive_g", g.dive_g);
	}
	applied_count_++;
	last_curve_ = curve;
	last_event_ = String("applied ") + s.id.c_str();
	shot_log(last_event_ + " vel=" + String(Variant(Vector3(g.vel.x, g.vel.y, g.vel.z))) + (t.serving ? " (serve)" : "") +
			(side ? String(" bend ") + (side > 0 ? "right" : "left") + " " + String::num(curve, 2) : String()));
	armed_ = -1;
}








void JVExtraShots::apply_side_spin(Node *ball, Node *me, Camera3D *cam, double raw_x, Vector3 aim, int kind, double power, const jvm::shots::ShotDef &s) {
	using namespace jvm::shots;
	Node3D *b3 = Object::cast_to<Node3D>(ball);
	if (!b3 || !cam || !ball->has_method("solve_launch")) {
		skipped_count_++; last_event_ = String("skipped ") + s.id.c_str() + ": no camera / Ball.solve_launch"; shot_log(last_event_); return;
	}

	const double sgn = raw_x >= 0 ? 1.0 : -1.0;
	Vec3 dir = { frame_.net_right.x * sgn, 0.0, frame_.net_right.z * sgn };
	if (dir.x == 0 && dir.z == 0) { Basis bs = cam->get_global_transform().basis; Vector3 cx = bs.get_column(0), cz = bs.get_column(2); dir = camera_relative_move({ cx.x, cx.y, cx.z }, { cz.x, cz.y, cz.z }, sgn, 0.0); }


	double charge = power;
	if (kind == 1 && me->has_method("hold_power")) charge = double(IO().call("player.input_state", me, "hold_power"));
	charge = CLAMP(charge, 0.0, 1.0);


	double q = 0.7;
	if (bool(IO().get("player.input_state", me, "last_perfect"))) q = 1.0;
	else if (me->has_method("_receive_quality")) q = double(IO().call("player.input_state", me, "_receive_quality"));
	Court court;
	if (IO().get("ball.flight", ball, "court_half_w").get_type() != Variant::NIL) {
		court.center_x = double(IO().get("ball.flight", ball, "court_center_x"));
		court.half_w = std::fmax(1.0, double(IO().get("ball.flight", ball, "court_half_w")));
		court.half_l = std::fmax(2.0, double(IO().get("ball.flight", ball, "court_half_l")));
	}


	Vector3 from = b3->get_global_position();
	Vec3 fwd = { aim.x - from.x, 0.0, aim.z - from.z };
	if (std::hypot(fwd.x, fwd.z) < 0.3) fwd = frame_.net_dir;
	SpinPath path = spin_path({ aim.x, 0, aim.z }, fwd, dir, charge, spin_, court);

	const double s_land = path.s_for(charge);
	Vec3 target = path.at(s_land);
	double scale = spin_spread_scale(q, charge, spin_);
	long long seed = (long long)(UtilityFunctions::randi() % 999983) + 1;


	SpinParams lsp = spin_;
	Vec3 ldir = dir;
	if (spin_.range_box) { lsp.spread_long = 0.0; ldir = path.tangent(s_land); }
	Vec3 land = spin_landing(target, ldir, scale, lsp, seed);
	const int side = raw_x >= 0 ? 1 : -1;
	Vector3 want(land.x, 0.0, land.z), aim_at = want;
	Vector3 v;
	double curve = 0.0;
	if (ball->has_method("stop_float")) IO().call("ball.float", ball, "stop_float");
	double best_err = 1e9; Vector3 best_v; double best_curve = 0.0;
	Vector3 best_aim = aim_at, best_dir_err;
	double ramp_A = 0.0, ramp_T = 1.0, ramp_sgn = 1.0;
	const bool can_ramp = ball->has_method("step_flight") && ball->has_method("flight_float_charge");
	if (can_ramp) {


		double radius = 0.13;
		if (Object *ph = IO().get("ball.physics", ball, "PHYS").operator Object *()) radius = double(IO().get("ball.physics", ph, "radius"));
		const Variant nhw = IO().get("ball.flight", ball, "net_half_width"), ccx = IO().get("ball.flight", ball, "court_center_x"), wmin = IO().get("ball.flight", ball, "wall_min"), wmax = IO().get("ball.flight", ball, "wall_max"), dome = IO().get("ball.flight", ball, "dome");
		const Variant sc0 = IO().get("ball.flight", ball, "stop_clock");
		const Variant fc0 = IO().call("ball.float", ball, "flight_float_charge");
		const double dt = 1.0 / 60.0;
		auto sim = [&](Vector3 at, double A, double T, int *steps_out) -> Vector3 {
			Vector3 lv = IO().call("ball.solver", ball, "solve_launch", from, at, kind, power, false, Vector2());
			double sg = world_curve(dir, { lv.x, lv.y, lv.z }, 1.0) >= 0 ? 1.0 : -1.0;
			Vector3 pos = from, vel = lv; Variant sc = sc0, fc = fc0;
			int n = 0;
			for (; n < 300; n++) {
				Vector3 before = pos;
				double c = sg * ramp_curve(A, path.k_time(), T, n * dt);
				Array st = IO().call("ball.solver", ball, "step_flight", pos, vel, c, dt, nhw, ccx, wmin, wmax, sc, fc, dome);
				if (st.size() < 5) break;
				pos = st[0]; vel = st[1]; sc = st[3]; fc = st[4];
				if (before.y > radius + 0.001 && pos.y <= radius + 0.02) { n++; break; }
			}
			if (steps_out) *steps_out = n;
			return pos;
		};
		int n0 = 0;
		sim(want, 0.0, 1.0, &n0);
		ramp_T = std::fmax(0.2, n0 * dt);
		RampSolve rs = solve_ramp([&](Vec3 at, double A) { Vector3 p = sim(Vector3(at.x, 0, at.z), A, ramp_T, nullptr); return Vec3{ p.x, p.y, p.z }; },
				{ want.x, 0, want.z }, dir, path.lateral(s_land));
		ramp_A = rs.A;
		best_aim = Vector3(rs.aim.x, 0, rs.aim.z);
		best_v = IO().call("ball.solver", ball, "solve_launch", from, best_aim, kind, power, false, Vector2());
		ramp_sgn = world_curve(dir, { best_v.x, best_v.y, best_v.z }, 1.0) >= 0 ? 1.0 : -1.0;
		best_curve = ramp_sgn * ramp_A;
		best_err = rs.err;
		if (OS::get_singleton()->has_environment("JV_SHOTS_DEBUG")) jvm::log("shots", String("curveball ramp: k ") + String::num(path.k_time(), 2) + (path.even ? " (even)" : "") + " A " + String::num(ramp_A, 3) + " T " + String::num(ramp_T, 2) + " err " + String::num(rs.err, 3) + " iters " + String::num_int64(rs.iters));
	} else {


		double gain = 1.0;
		for (int it = 0; it < 20; it++) {
			v = IO().call("ball.solver", ball, "solve_launch", from, aim_at, kind, power, false, Vector2());
			curve = world_curve(dir, { v.x, v.y, v.z }, spin_.curve_strength);
			if (!ball->has_method("_predict_land_curved")) { best_v = v; best_curve = curve; best_err = 0.0; break; }
			IO().set("ball.flight", ball, "vel", v); IO().set("ball.flight", ball, "curve", curve);
			Vector3 pred = IO().call("ball.solver", ball, "_predict_land_curved");
			Vector3 err(want.x - pred.x, 0, want.z - pred.z);
			if (err.length() < best_err) {
				best_err = err.length(); best_v = v; best_curve = curve; best_aim = aim_at; best_dir_err = err;
				if (best_err < 0.05) break;
				gain = std::fmin(1.0, gain * 1.5);
			} else {
				gain *= 0.5;
				if (gain < 0.02) break;
			}
			aim_at = best_aim + best_dir_err * gain;
		}
	}
	v = best_v; curve = best_curve;
	IO().set("ball.flight", ball, "vel", v);
	IO().set("ball.flight", ball, "curve", curve);
	IO().set("ball.flight", ball, "dive_g", 1.0);
	Dictionary d;
	d["dir"] = Vector3(dir.x, 0, dir.z); d["charge"] = charge; d["quality"] = q; d["scale"] = scale; d["seed"] = int64_t(seed);
	d["target"] = Vector3(target.x, 0, target.z); d["landing"] = want; d["aim"] = aim; d["side"] = side;
	d["aim_err"] = best_err;
	restore_marker();
	box_ = RangeBox();
	ramp_ = Ramp();
	if (can_ramp && std::fabs(ramp_A) > 1e-6) {
		ramp_.on = true; ramp_.ball = ball->get_instance_id(); ramp_.A = ramp_A; ramp_.k = path.k_time(); ramp_.T = ramp_T; ramp_.sgn = ramp_sgn;
		ramp_.seq = int64_t(IO().get("ball.flight", ball, "strike_seq")); ramp_.last_pos = from;
	}
	d["k"] = path.k_time(); d["even_bend"] = path.even; d["s_land"] = s_land; d["ramp_A"] = ramp_A; d["ramp_T"] = ramp_T; d["path_F"] = path.F; d["path_L"] = path.L;
	d["path_start"] = Vector3(path.c.x, 0, path.c.z); { Vec3 e = path.at(1.0); d["path_end"] = Vector3(e.x, 0, e.z); }
	d["seq"] = int64_t(IO().get("ball.flight", ball, "strike_seq"));
	if (spin_.range_box) {
		box_.on = true; box_.ball = ball->get_instance_id(); box_.center = Vector3(target.x, 0, target.z);
		box_.charge = charge; box_.seq = int64_t(IO().get("ball.flight", ball, "strike_seq"));

		box_.fseed = ball->has_method("float_seed_for") ? int64_t(IO().call("ball.float", ball, "float_seed_for", int64_t(seed), charge)) : 0;

		const double w = std::fmax(0.35, spin_.spread_short * scale);
		box_.spread = w;

		if (spin_display_ == "exact") build_bar(ball, path, s_land, w, box_.center);
	}
	d["range_box"] = box_.on;
	last_spin_ = d;
	applied_count_++;
	last_curve_ = curve;
	last_event_ = String("applied ") + s.id.c_str();
	shot_log(last_event_ + String(side > 0 ? " right" : " left") + " charge " + String::num(charge, 2) + " quality " + String::num(q, 2) +
			" target " + String(Variant(Vector3(target.x, 0, target.z))) + " landing " + String(Variant(want)) + " seed " + String::num_int64(seed));
	armed_ = -1;
}

void JVExtraShots::fire_rigid(const ShotDef &s) {
	TypedArray<Node> balls = jvm::find_targets(this, cfg_.get("ball", Variant()));
	TypedArray<Node> hitters = jvm::find_targets(this, cfg_.get("hitter", Variant()));
	if (balls.is_empty()) { jvm::log("shots", String("shot ") + s.id.c_str() + " pressed; no ball found"); return; }
	RigidBody3D *ball = Object::cast_to<RigidBody3D>(Object::cast_to<Node>(balls[0]));
	if (!ball) { jvm::warn("shots", "rigid backend: ball target is not a RigidBody3D"); return; }
	Vec3 fwd{ 0, 0, -1 };
	if (!hitters.is_empty()) {
		if (Node3D *h = Object::cast_to<Node3D>(Object::cast_to<Node>(hitters[0]))) {
			Vector3 f = -h->get_global_transform().basis.get_column(2);
			fwd = { f.x, f.y, f.z };
		}
	}
	Vec3 v = jvm::shots::launch_velocity(s, fwd);
	Vec3 w = jvm::shots::launch_spin(s, fwd);
	ball->set_linear_velocity(Vector3(v.x, v.y, v.z));
	ball->set_angular_velocity(Vector3(w.x, w.y, w.z));
	if (s.float_strength > 0) { float_shot_ = &s; float_t_ = 0; float_fwd_ = fwd; }
	jvm::log("shots", String("fired ") + s.id.c_str());
}

void JVExtraShots::_physics_process(double delta) {
	JVM_PROF("shots.physics");
	if (!follow_.applied) return;
	if (backend_ == "jv_ball") {
		update_ramp(delta);
		update_spin_display(delta);
		update_range_box(delta);


		for (uint64_t id : hooked_)
			if (Node *b = Object::cast_to<Node>(ObjectDB::get_instance(id)))
				if (Node *me = local_player_for(b)) if (bool(IO().get("player.input_state", me, "serving"))) { serving_seen_at_ = now(); serving_seen_[uint64_t(me->get_instance_id())] = now(); }
		return;
	}
	if (!float_shot_) return;
	float_t_ += delta;
	if (float_t_ > float_shot_->float_duration_s) { float_shot_ = nullptr; return; }
	TypedArray<Node> balls = jvm::find_targets(this, cfg_.get("ball", Variant()));
	RigidBody3D *ball = balls.is_empty() ? nullptr : Object::cast_to<RigidBody3D>(Object::cast_to<Node>(balls[0]));
	if (!ball) return;
	Vec3 a = jvm::shots::float_accel(*float_shot_, float_fwd_, float_t_);
	ball->apply_central_force(Vector3(a.x, a.y, a.z) * ball->get_mass());
}








void JVExtraShots::apply_rally_float(Node *ball, Node *me, Vector3 aim, int kind, double power, const ShotDef &s, int side) {
	Node3D *b3 = Object::cast_to<Node3D>(ball);
	if (!b3 || !ball->has_method("solve_float") || !ball->has_method("start_float") || !ball->has_method("float_seed_for")) {
		skipped_count_++; last_event_ = String("skipped ") + s.id.c_str() + ": no Ball.solve_float / start_float"; shot_log(last_event_); return;
	}
	double shake_from = 0.15, cap = 0.75;
	{
		Ref<Resource> feel = IO().resource("player.feel", "res://game/player/player_feel.tres");
		if (feel.is_valid()) {
			Variant a = IO().get("player.feel", feel.ptr(), "float_shake_from"), b = IO().get("player.feel", feel.ptr(), "float_power_cap");
			if (a.get_type() == Variant::FLOAT) shake_from = a;
			if (b.get_type() == Variant::FLOAT) cap = b;
		}
	}
	const double p = std::fmin(power, cap);
	const double charge = cap > shake_from ? CLAMP((p - shake_from) / (cap - shake_from), 0.0, 1.0) : 1.0;
	const int64_t pattern = int64_t(UtilityFunctions::randi() % 999983) + 1;
	const int64_t seed = IO().call("ball.float", ball, "float_seed_for", pattern, charge);
	const double fc = ball->has_method("float_charge_of") ? double(IO().call("ball.float", ball, "float_charge_of", seed)) : charge;
	Vector3 target(aim.x, 0.0, aim.z);


	if (side != 0) {

		Vector3 fz(frame_.net_dir.x, 0, frame_.net_dir.z);
		Vector3 c = b3->get_global_position();
		jvm::shots::Vec3 t = jvm::shots::float_side_target({ c.x, c.y, c.z }, { aim.x, 0, aim.z }, { fz.x, 0, fz.z }, side, float_lateral_);
		target = Vector3(t.x, 0.0, t.z);
	}
	Vector3 v = IO().call("ball.float", ball, "solve_float", b3->get_global_position(), target, p, fc);
	box_.on = false;
	IO().call("ball.float", ball, "stop_float");
	IO().set("ball.flight", ball, "vel", v);
	IO().call("ball.float", ball, "start_float", seed);
	Dictionary d; d["charge"] = charge; d["seed"] = seed; d["target"] = target; d["power"] = p; d["kind"] = kind; d["side"] = side; d["aim"] = Vector3(aim.x, 0, aim.z);
	last_float_ = d;
	applied_count_++;
	last_curve_ = 0.0;
	last_event_ = String("applied ") + s.id.c_str();
	shot_log(last_event_ + (side ? String(side > 0 ? " right" : " left") + " (to your side)" : String()) + " (the game's own float: charge " + String::num(charge, 2) + " seed " + String::num_int64(seed) + " vel " + String(Variant(v)) + ")");
	armed_ = -1;
}





double JVExtraShots::spike_boost_setting() {
	Object *a = IO().api();
	if (a && a->has_method("gameplay_get")) {
		Variant v = a->call("gameplay_get", "shots", "goofy.topspin_spike_boost");
		if (v.get_type() == Variant::FLOAT || v.get_type() == Variant::INT) return jvm::shots::clamp_spike_boost(double(v));
	}
	return jvm::shots::kSpikeBoostDefault;
}

void JVExtraShots::apply_topspin_cursor(Node *ball, Node *me, Camera3D *cam, double raw_x, int side, Vector3 aim, int kind, double power, const jvm::shots::ShotDef &s, double spike_boost) {
	using namespace jvm::shots;
	Node3D *b3 = Object::cast_to<Node3D>(ball);
	Object *phys = IO().get("ball.physics", ball, "PHYS").operator Object *();
	double g = 20.0, r = 0.26, net_h = 2.2;
	if (phys) {
		Variant a = IO().get("ball.physics", phys, "gravity"), b = IO().get("ball.physics", phys, "radius"), c = IO().get("ball.physics", phys, "net_height");
		if (a.get_type() == Variant::FLOAT) g = a;
		if (b.get_type() == Variant::FLOAT) r = b;
		if (c.get_type() == Variant::FLOAT) net_h = c;
	}
	Vector3 from = b3->get_global_position();
	Vector3 want(aim.x, 0.0, aim.z), aim_at = want;
	Vector3 vn = IO().call("ball.solver", ball, "solve_launch", from, want, kind, power, false, Vector2());
	if (ball->has_method("stop_float")) IO().call("ball.float", ball, "stop_float");
	Vector3 camx;
	if (cam) camx = cam->get_global_transform().basis.get_column(0);



	double cap = 2.5;
	if (phys) { Variant m = IO().get("ball.physics", phys, "serve_dive_max"); if (m.get_type() == Variant::FLOAT) cap = std::fmin(cap, double(m)); }
	const double dt = 1.0 / std::max(1, int(Engine::get_singleton()->get_physics_ticks_per_second()));
	const double clear_y = net_h + r + 0.15;
	auto solve = [&](Vector3 at, double k, double rise) { return solve_topspin({ from.x, from.y, from.z }, { at.x, 0, at.z }, { vn.x, vn.y, vn.z }, g, r, clear_y, k, rise, dt); };
	auto net_ok = [&](const TopspinSolve &ts) { return !ts.crosses_net || ts.net_y >= clear_y - 1e-6; };


	double use_k = 1.02, use_rise = 0.1, least = 1e9, fit_cap = cap;
	bool fit = false;
	struct Tier { std::vector<double> ks; double cap; };
	const Tier tiers[] = { { { 1.25, 1.2, 1.15, 1.1, 1.05, 1.02 }, cap }, { { 1.1, 1.05, 1.02 }, 6.0 }, { { 1.0, 0.95, 0.9, 0.85, 0.8, 0.7, 0.6, 0.5 }, cap } };
	for (auto &tr : tiers) {
		for (double k : tr.ks) {
			for (double rise : { 0.4, 0.3, 0.2, 0.1 }) {
				TopspinSolve ts = solve(want, k, rise);
				if (!net_ok(ts)) continue;
				if (ts.dive_g <= tr.cap) { use_k = k; use_rise = rise; fit = true; fit_cap = tr.cap; break; }
				if (ts.dive_g < least) { least = ts.dive_g; use_k = k; use_rise = rise; }
			}
			if (fit) break;
		}
		if (fit) break;
	}



	const double k_plain = use_k, rise_plain = use_rise;
	double boost_used = 1.0;
	String boost_note = spike_boost > 1.0 + 1e-9 ? String("not fitted") : String("off (not a topspin spike + air forward)");
	if (spike_boost > 1.0 + 1e-9) {



		fit_cap = std::fmax(fit_cap, 6.0);
		if (!fit) fit_cap = std::fmax(fit_cap, least);
		bool done = false;
		for (double part : { 1.0, 0.75, 0.5 }) {
			const double b = 1.0 + (spike_boost - 1.0) * part;
			for (double rise : { rise_plain, rise_plain - 0.1, rise_plain - 0.2, rise_plain - 0.3, 0.0 }) {
				if (rise < -1e-9) continue;
				TopspinSolve ts = solve(want, k_plain * b, rise);
				if (!net_ok(ts) || ts.dive_g > fit_cap + 1e-9) continue;
				use_k = k_plain * b; use_rise = rise; boost_used = b; done = true;
				break;
			}
			if (done) break;
		}
		boost_note = done ? (boost_used + 1e-9 >= spike_boost ? String("applied") : String("partial (dip cap)")) : String("skipped (dip cap / net)");
	}
	TopspinSolve best; double best_err = 1e9, best_curve = 0.0; Vector3 best_pred;
	for (int it = 0; it < 12; it++) {
		TopspinSolve ts = solve(aim_at, use_k, use_rise);
		double curve = 0.0;
		if (side != 0 && cam && s.combo.curve) {
			Vec3 w = { frame_.net_right.x * raw_x, 0.0, frame_.net_right.z * raw_x };
			curve = world_curve(w, ts.vel, curve_amount_);
		}
		IO().set("ball.flight", ball, "vel", Vector3(ts.vel.x, ts.vel.y, ts.vel.z)); IO().set("ball.flight", ball, "curve", curve); IO().set("ball.flight", ball, "dive_g", ts.dive_g);
		Vector3 pred = IO().call("ball.solver", ball, "_predict_land_curved");
		Vector3 err(want.x - pred.x, 0, want.z - pred.z);
		if (err.length() < best_err) { best_err = err.length(); best = ts; best_curve = curve; best_pred = pred; }
		if (best_err < 0.02) break;
		aim_at += err;
	}
	IO().set("ball.flight", ball, "vel", Vector3(best.vel.x, best.vel.y, best.vel.z));
	IO().set("ball.flight", ball, "curve", best_curve);
	IO().set("ball.flight", ball, "dive_g", best.dive_g);
	const double apex_n = vn.y > 0 ? from.y + vn.y * vn.y / (2 * g) : from.y;
	Dictionary d;
	d["target"] = want; d["predicted"] = Vector3(best_pred.x, 0, best_pred.z); d["err"] = best_err;
	d["speed"] = Vector3(best.vel.x, best.vel.y, best.vel.z).length(); d["normal_speed"] = vn.length();
	d["apex"] = best.apex; d["normal_apex"] = apex_n; d["net_y"] = best.net_y; d["net_clear"] = best.net_y - (net_h + r); d["dive_g"] = best.dive_g;
	d["hspeed"] = Vector2(best.vel.x, best.vel.z).length(); d["normal_hspeed"] = Vector2(vn.x, vn.z).length();
	d["speed_k"] = use_k; d["rise"] = use_rise; d["dive_cap"] = cap;
	d["touch_kind"] = kind; d["spike_boost"] = boost_used; d["spike_boost_wanted"] = spike_boost; d["spike_boost_note"] = boost_note; d["speed_k_plain"] = k_plain;
	last_topspin_ = d;
	applied_count_++;
	last_curve_ = best_curve;
	last_event_ = String("applied ") + s.id.c_str();
	shot_log(last_event_ + " to the cursor " + String(Variant(want)) + " (predicted landing off by " + String::num(best_err, 3) + " m) speed " + String::num(double(d["speed"]), 1) +
			" vs normal " + String::num(vn.length(), 1) + " m/s, apex " + String::num(best.apex, 2) + " vs normal " + String::num(apex_n, 2) + " m, dive_g " + String::num(best.dive_g, 2) + " (x" + String::num(use_k, 2) + " speed, +" + String::num(use_rise, 1) + " m)" +
			(best.crosses_net ? String(", net clearance ") + String::num(best.net_y - (net_h + r), 2) + " m" : String()) + (side ? String(side > 0 ? " bend right" : " bend left") : String()) +
			(spike_boost > 1.0 + 1e-9 ? String(", topspin spike boost x") + String::num(boost_used, 3) + " of x" + String::num(spike_boost, 3) + " (" + boost_note + ")" : String()));
	armed_ = -1;
}

Dictionary JVExtraShots::get_range_box() const {
	Dictionary d;
	d["on"] = box_.on; d["center"] = box_.center; d["pos"] = box_.pos; d["seen"] = box_.seen;
	d["fade"] = box_.on ? box_.last_fade : (spin_disp_.on ? spin_disp_.last_fade : box_.last_fade);
	d["spread"] = box_.spread; d["placed"] = box_.placed; d["stopped"] = box_.why;
	return d;
}

Dictionary JVExtraShots::get_spin_display() const {
	Dictionary d;
	d["on"] = spin_disp_.on; d["mode"] = spin_disp_.on ? spin_disp_.mode : spin_display_;
	d["style"] = spin_disp_.on ? spin_disp_.style : String();
	d["local"] = spin_disp_.local; d["shimmer"] = spin_disp_.shimmer; d["shake"] = spin_disp_.shake;
	d["question"] = spin_disp_.question_on; d["path"] = spin_disp_.path_on; d["box"] = spin_disp_.box_on;
	d["exact"] = spin_disp_.exact; d["bias"] = spin_disp_.bias; d["center"] = spin_disp_.center;
	d["question_pos"] = spin_disp_.question_pos; d["fade"] = spin_disp_.last_fade;
	d["box_start"] = spin_disp_.box_start; d["box_end"] = spin_disp_.box_end; d["box_half_w"] = spin_disp_.box_half_w;
	d["points"] = spin_disp_.path_points; d["seen"] = spin_disp_.seen; d["stopped"] = spin_disp_.why;
	d["curve_display_remote"] = curve_display_remote_; d["spin_display"] = spin_display_;
	d["question_visible"] = false;
	d["path_visible"] = path_mi_ && path_mi_->is_visible();
	d["arrow"] = curve_arrow_mi_ && curve_arrow_mi_->is_visible() && spin_disp_.box_on;
	d["box_visible"] = curve_box_mi_ && curve_box_mi_->is_visible() && spin_disp_.box_on;
	d["box_scale"] = Vector3(1, 1, 1);
	d["box_axis_z"] = spin_disp_.box_end - spin_disp_.box_start;
	if (curve_box_mi_ && spin_disp_.box_on) {
		d["box_scale"] = curve_box_mi_->get_global_transform().basis.get_scale();
		d["box_axis_z"] = curve_box_mi_->get_global_transform().basis.get_column(2);
	}
	return d;
}

static const double kSpinCurveEps = 0.05;
static const double kSpinBiasM = 0.55;

bool JVExtraShots::is_local_hitter(Node *ball) {
	if (!ball) return false;
	Variant hb = IO().get("ball.flight", ball, "hit_by");
	if (hb.get_type() != Variant::OBJECT) hb = IO().get("ball.flight", ball, "strike_source");
	Object *hitter = hb.get_type() == Variant::OBJECT ? hb.operator Object *() : nullptr;
	Node *me = local_player_for(ball);
	return hitter && me && hitter == me;
}

bool JVExtraShots::is_spin_flight(Node *ball) const {
	if (!ball) return false;
	if (bool(IO().get("ball.flight", ball, "float_serve"))) return false;
	if (bool(IO().get("ball.flight", ball, "frozen"))) return false;
	if (std::fabs(double(IO().get("ball.flight", ball, "curve"))) <= kSpinCurveEps) return false;
	Node3D *b3 = Object::cast_to<Node3D>(ball);
	if (!b3) return false;
	double radius = 0.13;
	if (Object *ph = IO().get("ball.physics", ball, "PHYS").operator Object *()) radius = double(IO().get("ball.physics", ph, "radius"));

	return b3->get_global_position().y > radius + 0.3;
}

Vector3 JVExtraShots::display_bias(int64_t seed) const {


	const uint32_t u = uint32_t(seed) * 2654435761u + 0x9E3779B9u;
	const double ang = (u % 360) * (Math_PI / 180.0);
	return Vector3(std::cos(ang), 0.0, std::sin(ang)) * kSpinBiasM;
}

PackedVector3Array JVExtraShots::sample_ground_path(Node *ball, Vector3 *out_land) const {
	PackedVector3Array pts;
	Node3D *b3 = Object::cast_to<Node3D>(ball);
	if (!b3) return pts;
	Vector3 pos = b3->get_global_position();
	Vector3 vel = IO().get("ball.flight", ball, "vel");
	double curve = double(IO().get("ball.flight", ball, "curve"));
	double radius = 0.13;
	if (Object *ph = IO().get("ball.physics", ball, "PHYS").operator Object *()) radius = double(IO().get("ball.physics", ph, "radius"));
	const double dt = 1.0 / 60.0;
	const bool can = ball->has_method("step_flight") && ball->has_method("flight_float_charge");
	Variant nhw, ccx, wmin, wmax, dome, sc, fc;
	if (can) {
		nhw = IO().get("ball.flight", ball, "net_half_width");
		ccx = IO().get("ball.flight", ball, "court_center_x");
		wmin = IO().get("ball.flight", ball, "wall_min");
		wmax = IO().get("ball.flight", ball, "wall_max");
		dome = IO().get("ball.flight", ball, "dome");
		sc = IO().get("ball.flight", ball, "stop_clock");
		fc = IO().call("ball.float", ball, "flight_float_charge");
	}
	const double CURVE_REF = 12.0, CURVE_DECAY = 1.2;
	double g = 11.0;
	if (Object *ph = IO().get("ball.physics", ball, "PHYS").operator Object *()) {
		Variant gv = IO().get("ball.physics", ph, "gravity");
		if (gv.get_type() != Variant::NIL) g = double(gv);
	}
	Vector3 last = Vector3(pos.x, 0, pos.z);
	pts.push_back(last);
	for (int n = 0; n < 300; n++) {
		Vector3 before = pos;
		if (can) {
			Array st = IO().call("ball.solver", ball, "step_flight", pos, vel, curve, dt, nhw, ccx, wmin, wmax, sc, fc, dome);
			if (st.size() < 5) break;
			pos = st[0]; vel = st[1]; sc = st[3]; fc = st[4];
		} else {

			Vector3 flat(vel.x, 0, vel.z);
			const double sp = flat.length();
			if (sp > 1.0) vel += Vector3(0, 1, 0).cross(flat / sp) * curve * (sp / CURVE_REF) * dt;
			curve *= std::exp(-dt / CURVE_DECAY);
			vel.y -= g * dt;
			pos += vel * dt;
		}
		Vector3 gnd(pos.x, 0, pos.z);
		if (gnd.distance_to(last) >= 0.25) { pts.push_back(gnd); last = gnd; }
		if (before.y > radius + 0.001 && pos.y <= radius + 0.02) { pts.push_back(gnd); break; }
	}
	if (out_land) *out_land = Vector3(pos.x, 0, pos.z);
	if (pts.size() < 2 && out_land) { pts.push_back(*out_land); }
	return pts;
}

Ref<ArrayMesh> JVExtraShots::make_path_mesh(const PackedVector3Array &pts, const Vector3 &origin) const {
	Ref<ArrayMesh> mesh;
	if (pts.size() < 2) return mesh;
	const double half = 0.12;
	PackedVector3Array v;
	PackedColorArray col;
	for (int i = 0; i + 1 < pts.size(); i++) {
		Vector3 a = pts[i] - origin, b = pts[i + 1] - origin;
		a.y = b.y = 0.0;
		Vector3 d = b - a;
		d.y = 0;
		if (d.length() < 1e-4) continue;
		Vector3 n = Vector3(-d.z, 0, d.x).normalized() * half;
		Vector3 a1 = a + n, a2 = a - n, b1 = b + n, b2 = b - n;

		a1.y = a2.y = b1.y = b2.y = 0.03;
		v.push_back(a1); v.push_back(b1); v.push_back(a2);
		v.push_back(a2); v.push_back(b1); v.push_back(b2);
	}
	if (v.is_empty()) return mesh;
	Array arr; arr.resize(Mesh::ARRAY_MAX);
	arr[Mesh::ARRAY_VERTEX] = v;
	mesh.instantiate();
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arr);
	return mesh;
}

Ref<ArrayMesh> JVExtraShots::make_question_mesh() const {


	auto box = [](PackedVector3Array &v, double x0, double z0, double x1, double z1, double y = 0.04) {
		const double xA = std::min(x0, x1), xB = std::max(x0, x1), zA = std::min(z0, z1), zB = std::max(z0, z1);
		Vector3 a(xA, y, zA), b(xB, y, zA), c(xB, y, zB), d(xA, y, zB);
		v.push_back(a); v.push_back(b); v.push_back(c);
		v.push_back(a); v.push_back(c); v.push_back(d);
	};
	PackedVector3Array v;

	box(v, -0.16, 0.18, 0.16, 0.28);
	box(v, 0.10, 0.02, 0.18, 0.18);
	box(v, -0.04, 0.02, 0.18, 0.10);
	box(v, -0.04, -0.10, 0.04, 0.10);
	box(v, -0.05, -0.28, 0.05, -0.16);
	Array arr; arr.resize(Mesh::ARRAY_MAX);
	arr[Mesh::ARRAY_VERTEX] = v;
	Ref<ArrayMesh> mesh; mesh.instantiate();
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arr);
	return mesh;
}

static Ref<StandardMaterial3D> make_spin_mat() {
	Ref<StandardMaterial3D> m; m.instantiate();
	m->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
	m->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
	m->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
	m->set_flag(BaseMaterial3D::FLAG_DISABLE_DEPTH_TEST, true);
	m->set_render_priority(10);
	return m;
}

void JVExtraShots::ensure_display_nodes(Node *ball) {
	if (!ball) return;
	if (!path_mat_.is_valid()) path_mat_ = make_spin_mat();
	if (!question_mat_.is_valid()) question_mat_ = make_spin_mat();
	auto parent = [&](Node3D *n) {
		if (!n) return;
		n->set_as_top_level(true);
		if (n->get_parent() != ball) {
			if (n->get_parent()) n->get_parent()->remove_child(n);
			ball->add_child(n);
		}
	};
	if (!path_mi_) {
		path_mi_ = memnew(MeshInstance3D);
		path_mi_->set_name("JVSpinPath");
		path_mi_->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
		path_mi_->set_material_override(path_mat_);
	}
	if (!question_mi_) {
		question_mi_ = memnew(MeshInstance3D);
		question_mi_->set_name("JVSpinQuestion");
		question_mi_->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
		question_mi_->set_material_override(question_mat_);
		question_mi_->set_mesh(make_question_mesh());
	}
	if (!question_lbl_) {
		question_lbl_ = memnew(Label3D);
		question_lbl_->set_name("JVSpinQuestionLabel");
		question_lbl_->set_text("?");
		question_lbl_->set_font_size(96);
		question_lbl_->set_pixel_size(0.012);
		question_lbl_->set_billboard_mode(BaseMaterial3D::BILLBOARD_ENABLED);
		question_lbl_->set_modulate(Color(1, 1, 1, 0.4));
		question_lbl_->set_outline_size(8);
		question_lbl_->set_draw_flag(Label3D::FLAG_DISABLE_DEPTH_TEST, true);
	}
	parent(path_mi_);
	parent(question_mi_);
	parent(question_lbl_);
}

void JVExtraShots::hide_game_marker(Node *ball, bool hide) {



	if (!hide || !ball) return;
	hide_native_landing_marker(ball);
}

void JVExtraShots::hide_native_landing_marker(Node *ball) {
	if (!ball) return;
	if (MeshInstance3D *lp = Object::cast_to<MeshInstance3D>(IO().get("ball.landing_marker", ball, "_land_predict").operator Object *()))
		lp->hide();
}

void JVExtraShots::drop_curve_box_nodes() {
	auto drop = [](Node *n) {
		if (!n) return;
		if (n->get_parent()) n->get_parent()->remove_child(n);
		n->queue_free();
	};

	drop(curve_box_mi_);
	curve_box_mi_ = nullptr;
	curve_box_shell_ = nullptr;
	curve_arrow_mi_ = nullptr;
	curve_box_mat_.unref();
	curve_box_shell_mat_.unref();
	curve_arrow_mat_.unref();
}

void JVExtraShots::restore_remote_box_marker(Node *ball) {


	(void)ball;
	drop_curve_box_nodes();
}

bool JVExtraShots::resolve_curve_range(Node *ball, const Vector3 &land, Vector3 &start, Vector3 &end) const {
	start = Vector3(); end = Vector3();
	const int64_t cur_seq = int64_t(IO().get("ball.flight", ball, "strike_seq"));
	const bool same_strike = last_spin_.has("seq") && int64_t(last_spin_["seq"]) == cur_seq;
	if (same_strike && last_spin_.has("path_start") && last_spin_.has("path_end") && last_spin_.has("landing")) {
		Vector3 L = last_spin_["landing"];
		if (Vector2(L.x - land.x, L.z - land.z).length() < 0.85) {
			start = last_spin_["path_start"];
			end = last_spin_["path_end"];
			start.y = 0; end.y = 0;
			if ((end - start).length() > 0.15) return true;
		}
	}
	Variant hb = IO().get("ball.flight", ball, "hit_by");
	if (hb.get_type() != Variant::OBJECT) hb = IO().get("ball.flight", ball, "strike_source");
	Node *hitter = hb.get_type() == Variant::OBJECT ? Object::cast_to<Node>(hb.operator Object *()) : nullptr;
	Vector3 aim;
	bool have_aim = false;
	if (hitter) {
		Variant av = IO().get("player.input_state", hitter, "aim_point");
		if (av.get_type() == Variant::VECTOR3) {
			aim = av;
			aim.y = 0;
			have_aim = std::hypot(double(aim.x), double(aim.z)) > 0.2;
		}
	}
	if (!have_aim) {


		Node3D *b3 = Object::cast_to<Node3D>(ball);
		Vector3 vel = IO().get("ball.flight", ball, "vel");
		Vector3 flat(vel.x, 0, vel.z);
		Vector3 along = flat.length() > 0.2 ? flat.normalized() : Vector3(0, 0, (b3 && b3->get_global_position().z > 0 ? -1.0 : 1.0));
		Vector3 right = Vector3(0, 1, 0).cross(along);
		if (right.length() < 0.05) right = Vector3(1, 0, 0);
		right.normalize();
		if (double(IO().get("ball.flight", ball, "curve")) < 0.0) right = -right;
		const double span = std::fmax(spin_.max_lateral, 1.0);
		start = Vector3(land.x - right.x * (span * 0.5), 0, land.z - right.z * (span * 0.5));
		end = Vector3(land.x + right.x * (span * 0.5), 0, land.z + right.z * (span * 0.5));
		return (end - start).length() > 0.15;
	}
	Node3D *b3 = Object::cast_to<Node3D>(ball);
	Vector3 from = b3 ? b3->get_global_position() : aim;
	jvm::shots::Vec3 fwd{ aim.x - from.x, 0, aim.z - from.z };
	if (std::hypot(fwd.x, fwd.z) < 0.3) fwd = { 0, 0, (from.z > 0 ? -1.0 : 1.0) };
	Vector3 vel = IO().get("ball.flight", ball, "vel");
	Vector3 flat(vel.x, 0, vel.z);
	Vector3 right = Vector3(0, 1, 0).cross(flat.length() > 0.2 ? flat : Vector3(fwd.x, 0, fwd.z));
	if (right.length() < 0.05) right = Vector3(-fwd.z, 0, fwd.x);
	right.y = 0;
	if (right.length() > 0.01) right.normalize();
	jvm::shots::Court court;
	if (IO().get("ball.flight", ball, "court_half_w").get_type() != Variant::NIL) {
		court.center_x = double(IO().get("ball.flight", ball, "court_center_x"));
		court.half_w = std::fmax(1.0, double(IO().get("ball.flight", ball, "court_half_w")));
		court.half_l = std::fmax(2.0, double(IO().get("ball.flight", ball, "court_half_l")));
	}
	auto dist_seg = [&](const jvm::shots::Vec3 &e) {
		Vector3 a(aim.x, 0, aim.z), b(e.x, 0, e.z);
		Vector3 ab = b - a;
		double t = ab.length_squared() < 1e-6 ? 0.0 : CLAMP(double((land - a).dot(ab) / ab.length_squared()), 0.0, 1.0);
		return (a + ab * t - land).length();
	};
	jvm::shots::Vec3 dir{ right.x, 0, right.z };
	jvm::shots::SpinPath p1 = jvm::shots::spin_path({ aim.x, 0, aim.z }, fwd, dir, 1.0, spin_, court);
	jvm::shots::SpinPath p2 = jvm::shots::spin_path({ aim.x, 0, aim.z }, fwd, { -dir.x, 0, -dir.z }, 1.0, spin_, court);
	jvm::shots::Vec3 e1 = p1.at(1.0), e2 = p2.at(1.0);
	start = aim;
	end = dist_seg(e1) <= dist_seg(e2) ? Vector3(e1.x, 0, e1.z) : Vector3(e2.x, 0, e2.z);
	return (end - start).length() > 0.15;
}




static void append_rounded_tri(PackedVector3Array &verts, Vector2 a, Vector2 b, Vector2 c) {
	const double ROUND = 0.36;
	Vector2 corners[3] = { a, b, c };
	Vector2 ctr = (a + b + c) / 3.0;
	Vector2 rim[12];
	int n = 0;
	for (int i = 0; i < 3; i++) {
		Vector2 p = corners[i];
		Vector2 prev = corners[(i + 2) % 3];
		Vector2 nxt = corners[(i + 1) % 3];
		double r = std::min(p.distance_to(prev), p.distance_to(nxt)) * ROUND;
		Vector2 t_in = p + (prev - p).normalized() * r;
		Vector2 t_out = p + (nxt - p).normalized() * r;
		for (int k = 0; k < 4; k++) {
			double u = double(k) / 3.0, w = 1.0 - u;
			rim[n++] = w * w * t_in + 2.0 * w * u * p + u * u * t_out;
		}
	}
	for (int i = 0; i < n; i++) {
		Vector2 p0 = rim[i], p1 = rim[(i + 1) % n];
		verts.push_back(Vector3(ctr.x, 0, ctr.y));
		verts.push_back(Vector3(p0.x, 0, p0.y));
		verts.push_back(Vector3(p1.x, 0, p1.y));
	}
}

static void append_native_shard(PackedVector3Array &verts, Vector2 tip, Vector2 dir, double inset) {
	const double base_out = 0.34, half_b = 0.13;
	if (dir.length_squared() < 1e-10) return;
	dir.normalize();
	Vector2 perp(-dir.y, dir.x);
	Vector2 c0 = tip;
	Vector2 c1 = tip + dir * base_out + perp * half_b;
	Vector2 c2 = tip + dir * base_out - perp * half_b;
	if (inset > 0.0) {
		Vector2 ctr = (c0 + c1 + c2) / 3.0;
		double s = 1.0 - inset;
		c0 = ctr + (c0 - ctr) * s;
		c1 = ctr + (c1 - ctr) * s;
		c2 = ctr + (c2 - ctr) * s;
	}
	append_rounded_tri(verts, c0, c1, c2);
}

Ref<ArrayMesh> JVExtraShots::make_curve_box_mesh(double half_w, double half_l, double inset) const {
	Ref<ArrayMesh> mesh;
	PackedVector3Array verts;
	const double sx[4] = { 1, -1, -1, 1 };
	const double sz[4] = { 1, 1, -1, -1 };
	for (int i = 0; i < 4; i++) {
		Vector2 tip(sx[i] * half_w, sz[i] * half_l);
		Vector2 dir = tip.length_squared() > 1e-10 ? tip.normalized() : Vector2(sx[i], sz[i]).normalized();
		append_native_shard(verts, tip, dir, inset);
	}
	if (verts.is_empty()) return mesh;
	Array arr;
	arr.resize(Mesh::ARRAY_MAX);
	arr[Mesh::ARRAY_VERTEX] = verts;
	mesh.instantiate();
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arr);
	return mesh;
}

void JVExtraShots::place_float_arrow(Node *ball, MeshInstance3D *lp, const Vector3 &land, const Basis &basis, const Vector3 &origin, bool show) {
	if (!lp) return;
	if (!curve_arrow_mi_) {
		curve_arrow_mi_ = memnew(MeshInstance3D);
		curve_arrow_mi_->set_name("JVCurveArrow");
		curve_arrow_mi_->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
		Ref<QuadMesh> quad;
		quad.instantiate();
		quad->set_size(Vector2(0.62, 0.9));
		curve_arrow_mi_->set_mesh(quad);
		Ref<Shader> sh = ResourceLoader::get_singleton()->load("res://game/fx/waypoint_arrow.gdshader");
		if (sh.is_valid()) {
			curve_arrow_mat_.instantiate();
			curve_arrow_mat_->set_shader(sh);
			curve_arrow_mat_->set_render_priority(3);
			curve_arrow_mi_->set_material_override(curve_arrow_mat_);
		}
		lp->add_child(curve_arrow_mi_);
	}
	auto K = [&](const char *k, double def) -> double {
		if (!ball) return def;
		Ref<Script> sc = ball->get_script();
		if (sc.is_valid()) {
			Dictionary m = sc->get_script_constant_map();
			if (m.has(k)) return double(m[k]);
		}
		return def;
	};
	const double arrow_h = K("LAND_ARROW_H", 1.15);
	Vector3 world = Vector3(land.x, origin.y + arrow_h, land.z);
	Vector3 local = basis.xform_inv(world - origin);
	curve_arrow_mi_->set_position(local);
	curve_arrow_mi_->set_scale(Vector3(1, 1, 1));
	curve_arrow_mi_->set_rotation(Vector3());
	curve_arrow_mi_->reset_physics_interpolation();
	if (curve_arrow_mat_.is_valid()) {
		Color solid(1, 1, 1, 1), edge(0.05, 0.07, 0.11, 0.94);
		if (curve_box_mat_.is_valid()) {
			solid = curve_box_mat_->get_albedo();
			solid.a = 1.0;
			edge = Color(solid.r, solid.g, solid.b, 0.94).darkened(0.88);
		}
		curve_arrow_mat_->set_shader_parameter("tint", solid);
		curve_arrow_mat_->set_shader_parameter("edge", edge);
	}
	if (show) curve_arrow_mi_->show();
	else curve_arrow_mi_->hide();
}

void JVExtraShots::drive_remote_uncertainty_box(Node *ball, const Vector3 &start, const Vector3 &end, double fade, const Color &col) {
	if (!ball) return;
	hide_native_landing_marker(ball);
	auto K = [&](const char *k, double def) -> double {
		Ref<Script> sc = ball->get_script();
		if (sc.is_valid()) { Dictionary m = sc->get_script_constant_map(); if (m.has(k)) return double(m[k]); }
		return def;
	};
	const double spread = K("SPREAD_FAR", 1.15);
	spin_disp_.box_half_w = spread / std::sqrt(2.0);
	Vector3 a(start.x, 0, start.z), b(end.x, 0, end.z);
	Vector3 mid((a.x + b.x) * 0.5, 0.045, (a.z + b.z) * 0.5);
	Vector3 along = b - a;
	double len = along.length();
	if (len < 0.2) { along = Vector3(0, 0, 1); len = 0.2; }
	along /= len;
	Vector3 across = Vector3(0, 1, 0).cross(along);
	if (across.length() < 0.01) across = Vector3(1, 0, 0);
	across.normalize();
	Basis basis;
	basis.set_column(0, across);
	basis.set_column(1, Vector3(0, 1, 0));
	basis.set_column(2, along);
	const double half = spin_disp_.box_half_w > 0.1 ? spin_disp_.box_half_w : (spread / std::sqrt(2.0));
	if (!curve_box_mi_) {
		curve_box_mi_ = memnew(MeshInstance3D);
		curve_box_mi_->set_name("JVCurveBox");
		curve_box_mi_->set_as_top_level(true);
		curve_box_mi_->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
		curve_box_mat_.instantiate();
		curve_box_mat_->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
		curve_box_mat_->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
		curve_box_mat_->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
		curve_box_mat_->set_render_priority(2);
		curve_box_mi_->set_material_override(curve_box_mat_);
		ball->add_child(curve_box_mi_);
		curve_box_shell_ = memnew(MeshInstance3D);
		curve_box_shell_->set_name("JVCurveBoxShell");
		curve_box_shell_->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
		curve_box_shell_mat_.instantiate();
		curve_box_shell_mat_->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
		curve_box_shell_mat_->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
		curve_box_shell_mat_->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
		curve_box_shell_mat_->set_render_priority(1);
		curve_box_shell_->set_material_override(curve_box_shell_mat_);
		curve_box_shell_->set_position(Vector3(0.0, -0.007, 0.0));
		curve_box_mi_->add_child(curve_box_shell_);
	}
	curve_box_mi_->set_mesh(make_curve_box_mesh(half, len * 0.5, 0.22));
	if (curve_box_shell_) {
		curve_box_shell_->set_mesh(make_curve_box_mesh(half, len * 0.5, 0.0));
		curve_box_shell_->set_position(Vector3(0.0, -0.007, 0.0));
		curve_box_shell_->set_scale(Vector3(1, 1, 1));
		curve_box_shell_->show();
	}
	curve_box_mi_->set_global_transform(Transform3D(basis, mid));
	curve_box_mi_->reset_physics_interpolation();
	curve_box_mi_->show();
	if (curve_box_mat_.is_valid())
		curve_box_mat_->set_albedo(Color(col.r, col.g, col.b, col.a));
	if (curve_box_shell_mat_.is_valid())
		curve_box_shell_mat_->set_albedo(Color(0.05, 0.07, 0.11, 0.95 * fade));
	place_float_arrow(ball, curve_box_mi_, spin_disp_.exact, basis, mid, spin_disp_.question_on);
}

void JVExtraShots::clear_spin_display(const String &why) {
	if (!why.is_empty()) spin_disp_.why = why;
	drop_curve_box_nodes();
	spin_disp_.on = false;
	spin_disp_.question_on = false;
	spin_disp_.path_on = false;
	spin_disp_.box_on = false;
	spin_disp_.path_points = Array();
	auto drop = [](Node *n) {
		if (!n) return;
		if (n->get_parent()) n->get_parent()->remove_child(n);
		n->queue_free();
	};
	drop(path_mi_); path_mi_ = nullptr;
	drop(question_mi_); question_mi_ = nullptr;
	drop(question_lbl_); question_lbl_ = nullptr;
}

void JVExtraShots::update_spin_display(double delta) {


	Node *active = nullptr;
	for (uint64_t id : hooked_) {
		if (Node *b = Object::cast_to<Node>(ObjectDB::get_instance(id))) {
			if (is_spin_flight(b)) { active = b; break; }
		}
	}
	if (!active) {
		if (spin_disp_.on) {
			if (box_.on && box_.ball == spin_disp_.ball) { box_.on = false; box_.why = "flight ended"; }
			clear_spin_display(spin_disp_.why.is_empty() ? String("flight ended") : spin_disp_.why);
		}
		return;
	}
	const int64_t seq = int64_t(IO().get("ball.flight", active, "strike_seq"));
	if (spin_disp_.on && spin_disp_.ball == active->get_instance_id() && spin_disp_.seq != seq) {



		drop_curve_box_nodes();
		spin_disp_ = SpinDisplay();
	}
	if (!spin_disp_.on || spin_disp_.ball != active->get_instance_id() || spin_disp_.seq != seq) {
		drop_curve_box_nodes();
		spin_disp_ = SpinDisplay();
		spin_disp_.on = true;
		spin_disp_.ball = active->get_instance_id();
		spin_disp_.seq = seq;
		spin_disp_.fseed = (seq == 0 ? 1 : seq);
		spin_disp_.bias = display_bias(spin_disp_.fseed);
		spin_disp_.mode = spin_display_;
	}
	spin_disp_.seen += delta;
	spin_disp_.local = is_local_hitter(active);


	spin_disp_.style = (curve_display_remote_ == "off") ? String("off") : String("full");
	spin_disp_.mode = spin_display_;
	spin_disp_.shimmer = true;
	spin_disp_.shake = true;

	Vector3 land;
	sample_ground_path(active, &land);
	if (active->has_method("_predict_land_curved")) {
		Vector3 pred = IO().call("ball.solver", active, "_predict_land_curved");
		if (std::isfinite(pred.x) && std::isfinite(pred.z)) land = Vector3(pred.x, 0, pred.z);
	}
	spin_disp_.exact = land;

	Vector3 jitter;
	if (spin_disp_.shake && active->has_method("float_offset")) {
		Vector3 vel = IO().get("ball.flight", active, "vel");
		jitter = IO().call("ball.float", active, "float_offset", spin_disp_.fseed, spin_disp_.seen, vel);
		jitter.y = 0.0;
	}

	jitter *= 0.35;
	spin_disp_.center = Vector3(land.x + jitter.x, 0.04, land.z + jitter.z);
	spin_disp_.question_pos = spin_disp_.center;
	spin_disp_.path_points = Array();


	auto K = [&](const char *k, double def) -> double {
		Ref<Script> sc = active->get_script();
		if (sc.is_valid()) { Dictionary m = sc->get_script_constant_map(); if (m.has(k)) return double(m[k]); }
		return def;
	};
	const double from_still = K("FLOAT_MARK_FROM_STILL", 0.05), from = K("FLOAT_MARK_FROM", 0.12);
	const double gone_still = K("FLOAT_MARK_GONE_STILL", 0.25), gone_full = K("FLOAT_MARK_GONE", 0.35);
	double radius = 0.13;
	if (Object *ph = IO().get("ball.physics", active, "PHYS").operator Object *()) radius = double(IO().get("ball.physics", ph, "radius"));
	const Vector3 vel = IO().get("ball.flight", active, "vel");
	double left = 0.0;
	if (active->has_method("float_fall_time")) left = double(IO().call("ball.float", active, "float_fall_time", Object::cast_to<Node3D>(active)->get_global_position().y, vel.y, radius, 1.0));
	const double done = spin_disp_.seen / std::fmax(spin_disp_.seen + left, 0.05);
	const double gone = Math::smoothstep(Math::lerp(from_still, from, 0.8), Math::lerp(gone_still, gone_full, 0.8), done);
	const double shimmer = spin_disp_.shimmer ? (0.82 + 0.18 * std::sin(now() * 11.0)) : 1.0;
	double fade = Math::lerp(0.9, 0.03, gone) * (spin_disp_.shimmer ? shimmer : 0.85);
	spin_disp_.last_fade = fade;

	const bool question = spin_disp_.mode == "question";
	const bool show = spin_disp_.style != "off" && fade > 0.02;
	spin_disp_.path_on = false;
	spin_disp_.question_on = show && question;
	spin_disp_.box_on = show;
	if (path_mi_) path_mi_->hide();

	if (!show) {
		drop_curve_box_nodes();
		hide_native_landing_marker(active);
		if (question_mi_) question_mi_->hide();
		if (question_lbl_) question_lbl_->hide();
		spin_disp_.box_on = false;
		return;
	}



	Vector3 b0 = spin_disp_.box_start, b1 = spin_disp_.box_end;
	const bool frozen = (b1 - b0).length() > 0.15;
	if (!frozen) {
		if (resolve_curve_range(active, land, b0, b1)) {
			spin_disp_.box_start = b0;
			spin_disp_.box_end = b1;
		} else {
			spin_disp_.box_on = false;
			hide_game_marker(active, true);
			b0 = b1 = Vector3();
		}
	}
	if ((b1 - b0).length() > 0.15) {
		Color box_col = Color(0.4, 1.0, 1.0, 0.92 * fade);
		drive_remote_uncertainty_box(active, b0, b1, fade, box_col);
	} else {
		spin_disp_.box_on = false;
		hide_game_marker(active, true);
	}
	if (question_mi_) question_mi_->hide();
	if (question_lbl_) question_lbl_->hide();
}








void JVExtraShots::update_range_box(double delta) {

	if (spin_disp_.on) return;
	if (!box_.on) { restore_marker(); return; }
	Node3D *b = Object::cast_to<Node3D>(ObjectDB::get_instance(box_.ball));
	if (!b) { box_.on = false; box_.why = "ball gone"; return; }
	MeshInstance3D *lp = Object::cast_to<MeshInstance3D>(IO().get("ball.landing_marker", b, "_land_predict").operator Object *());
	if (!lp) { box_.on = false; box_.why = "no Ball._land_predict"; return; }


	if (int64_t(IO().get("ball.flight", b, "strike_seq")) != box_.seq) {
		if (box_.seen < 0.15) box_.seq = int64_t(IO().get("ball.flight", b, "strike_seq"));
		else { box_.on = false; box_.why = "touched again (strike_seq)"; return; }
	}
	if (bool(IO().get("ball.flight", b, "float_serve"))) { box_.on = false; box_.why = "ball floats"; return; }
	box_.seen += delta;

	Dictionary consts;
	{ Ref<Script> sc = b->get_script(); if (sc.is_valid()) consts = sc->get_script_constant_map(); }

	const bool question_hides = spin_display_ == "question" && std::fabs(double(IO().get("ball.flight", b, "curve"))) > 0.05;
	if (!lp->is_visible() && !question_hides) { if (box_.seen > 0.05) { box_.on = false; box_.why = "marker hidden (landed / hints off)"; } return; }
	if (!lp->is_visible() && question_hides) {

	}
	auto K = [&](const char *k, double def) -> double {
		if (consts.has(k)) return double(consts[k]);
		return def;
	};
	const double from_still = K("FLOAT_MARK_FROM_STILL", 0.05), from = K("FLOAT_MARK_FROM", 0.12);
	const double gone_still = K("FLOAT_MARK_GONE_STILL", 0.25), gone_full = K("FLOAT_MARK_GONE", 0.35);
	const double far = K("SPREAD_FAR", 1.15), near_ = K("SPREAD_NEAR", 0.42), fspread = K("FLOAT_MARK_SPREAD", 1.9);
	const Vector3 vel = IO().get("ball.flight", b, "vel");
	double radius = 0.13;
	if (Object *ph = IO().get("ball.physics", b, "PHYS").operator Object *()) radius = double(IO().get("ball.physics", ph, "radius"));
	double left = 0.0;
	if (b->has_method("float_fall_time")) left = double(IO().call("ball.float", b, "float_fall_time", b->get_global_position().y, vel.y, radius, 1.0));
	double closeness = 0.0;
	if (b->has_method("_time_to_floor")) { double tf = IO().call("ball.solver", b, "_time_to_floor"); if (tf >= 0.0) closeness = CLAMP(1.0 - tf / 0.9, 0.0, 1.0); }
	Color col = closeness < 0.5 ? Color(0.4, 1.0, 1.0).lerp(Color(1.0, 0.75, 0.15), closeness * 2.0)
								: Color(1.0, 0.75, 0.15).lerp(Color(1.0, 0.2, 0.12), (closeness - 0.5) * 2.0);
	col.a = Math::lerp(0.82, 0.98, closeness);
	const double c = CLAMP(box_.charge, 0.0, 1.0);
	const double done = box_.seen / std::fmax(box_.seen + left, 0.05);
	const double gone = Math::smoothstep(Math::lerp(from_still, from, c), Math::lerp(gone_still, gone_full, c), done);


	const bool question = spin_display_ == "question" && std::fabs(double(IO().get("ball.flight", b, "curve"))) > 0.05;
	const String style = (spin_disp_.on && spin_disp_.ball == box_.ball) ? spin_disp_.style : (is_local_hitter(b) ? String("full") : curve_display_remote_);
	const bool full = style == "full";
	const double shimmer = full ? (0.82 + 0.18 * std::sin(now() * 11.0)) : 1.0;
	double fade = Math::lerp(0.9, 0.03, gone) * (full ? shimmer : 0.85);
	if (!full) {
		const double g = col.r * 0.3 + col.g * 0.59 + col.b * 0.11;
		col = Color(g, g, g, col.a * 0.45);
		fade *= 0.7;
	}
	box_.last_fade = fade;



	(void)question;
	(void)lp;
}






void JVExtraShots::build_bar(Node *ball, const jvm::shots::SpinPath &path, double s_land, double w, Vector3 center) {
	using namespace jvm::shots;
	bar_on_ = false; bar_mesh_.unref(); bar_shell_.unref(); bar_points_.clear();
	if (!ball->has_method("_shard_cluster_mesh")) return;
	std::vector<BarShard> shards = spin_bar_shards(path, w, 0.9);
	for (int i = 0; i <= 32; i++) { Vec3 a = path.at(i / 32.0); bar_points_.push_back(Vector3(a.x, 0, a.z)); }
	auto make = [&](double inset) -> Ref<ArrayMesh> {
		Ref<ArrayMesh> tm = IO().call("ball.landing_marker", ball, "_shard_cluster_mesh", inset, 1.0);
		if (tm.is_null() || tm->get_surface_count() < 1) return Ref<ArrayMesh>();
		Array arr = tm->surface_get_arrays(0);
		PackedVector3Array tv = arr[Mesh::ARRAY_VERTEX];
		const int per = tv.size() / 4;
		if (per <= 0) return Ref<ArrayMesh>();
		const Vector3 tip0(0.70711, 0.0, 0.70711);
		const double a0 = std::atan2(-0.70711, -0.70711);
		PackedVector3Array out;
		for (const BarShard &sh : shards) {
			const double th = std::atan2(sh.point.z, sh.point.x) - a0, cs = std::cos(th), sn = std::sin(th);
			for (int i = 0; i < per; i++) {
				Vector3 q = tv[i] - tip0;
				out.push_back(Vector3(sh.tip.x + q.x * cs - q.z * sn - center.x, 0.0, sh.tip.z + q.x * sn + q.z * cs - center.z));
			}
		}
		Array na; na.resize(Mesh::ARRAY_MAX); na[Mesh::ARRAY_VERTEX] = out;
		Ref<ArrayMesh> m; m.instantiate();
		m->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, na);
		return m;
	};
	bar_mesh_ = make(0.22); bar_shell_ = make(0.0);
	bar_on_ = bar_mesh_.is_valid();
	bar_ball_ = ball->get_instance_id();
}


void JVExtraShots::restore_marker() {
	if (!bar_on_ && bar_ball_ == 0) return;
	bar_on_ = false;
	Node *b = Object::cast_to<Node>(ObjectDB::get_instance(bar_ball_));
	bar_ball_ = 0;
	if (!b || !b->has_method("_shard_cluster_mesh")) return;



	const double sp = double(IO().get("ball.landing_marker", b, "_predict_spread"));
	if (bar_mesh_.is_valid())
		if (MeshInstance3D *lp = Object::cast_to<MeshInstance3D>(IO().get("ball.landing_marker", b, "_land_predict").operator Object *()))
			if (lp->get_mesh() == bar_mesh_)
				lp->set_mesh(IO().call_undo("ball.landing_marker", b, "_shard_cluster_mesh", 0.22, sp));
	if (bar_shell_.is_valid())
		if (MeshInstance3D *sh = Object::cast_to<MeshInstance3D>(IO().get("ball.landing_marker", b, "_land_shell").operator Object *()))
			if (sh->get_mesh() == bar_shell_)
				sh->set_mesh(IO().call_undo("ball.landing_marker", b, "_shard_cluster_mesh", 0.0, sp));
}




void JVExtraShots::update_ramp(double delta) {
	if (!ramp_.on) return;
	Node3D *b = Object::cast_to<Node3D>(ObjectDB::get_instance(ramp_.ball));
	if (!b) { ramp_.on = false; ramp_.why = "ball gone"; return; }
	if (int64_t(IO().get("ball.flight", b, "strike_seq")) != ramp_.seq) {
		if (ramp_.seen < 0.15) ramp_.seq = int64_t(IO().get("ball.flight", b, "strike_seq"));
		else { ramp_.on = false; ramp_.why = "touched again"; return; }
	}
	if (bool(IO().get("ball.flight", b, "frozen"))) { ramp_.on = false; ramp_.why = "frozen"; return; }
	ramp_.seen += delta;
	Vector3 p = b->get_global_position();
	if (p != ramp_.last_pos) { ramp_.steps++; ramp_.last_pos = p; }
	double radius = 0.13;
	if (Object *ph = IO().get("ball.physics", b, "PHYS").operator Object *()) radius = double(IO().get("ball.physics", ph, "radius"));
	if (p.y <= radius + 0.02 || ramp_.steps > 400) { ramp_.on = false; ramp_.why = "landed"; return; }
	IO().set("ball.flight", b, "curve", ramp_.sgn * jvm::shots::ramp_curve(ramp_.A, ramp_.k, ramp_.T, ramp_.steps / 60.0));
}

Dictionary JVExtraShots::get_spin_bar() const {
	Dictionary d;
	d["on"] = bar_on_; d["points"] = bar_points_;
	d["mesh_vertices"] = bar_mesh_.is_valid() && bar_mesh_->get_surface_count() > 0 ? int64_t(PackedVector3Array(bar_mesh_->surface_get_arrays(0)[Mesh::ARRAY_VERTEX]).size()) : int64_t(0);
	d["ramp_on"] = ramp_.on; d["ramp_steps"] = ramp_.steps; d["ramp_A"] = ramp_.A; d["ramp_k"] = ramp_.k; d["ramp_T"] = ramp_.T; d["ramp_stopped"] = ramp_.why;
	return d;
}







static String key_label(const String &action) {
	InputMap *im = InputMap::get_singleton();
	if (!im->has_action(action)) return "?";
	TypedArray<InputEvent> evs = im->action_get_events(action);
	for (int i = 0; i < evs.size(); i++) {
		Ref<InputEventKey> k = evs[i];
		if (k.is_valid()) {
			Key code = k->get_keycode() != KEY_NONE ? k->get_keycode() : k->get_physical_keycode();
			return OS::get_singleton()->get_keycode_string(code);
		}
	}
	return evs.size() ? String("pad/mouse") : String("unbound");
}

void JVExtraShots::_input(const Ref<InputEvent> &e) {
	Ref<InputEventKey> k = e;
	if (k.is_valid() && k->is_pressed() && !k->is_echo() && k->get_keycode() == dbg_key_) set_debug_visible(!dbg_on_);
}

void JVExtraShots::set_debug_visible(bool on) {
	if (on == dbg_on_ && (!on || dbg_layer_)) return;
	dbg_on_ = on;
	if (on && !dbg_layer_) {
		CanvasLayer *cl = memnew(CanvasLayer);
		cl->set_layer(127);
		cl->set_name("JVShotDebugger");
		ColorRect *bg = memnew(ColorRect);
		bg->set_color(Color(0, 0, 0, 0.55));
		bg->set_position(Vector2(8, 70));
		bg->set_size(Vector2(760, 330));
		bg->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		cl->add_child(bg);
		Label *l = memnew(Label);
		l->set_position(Vector2(16, 76));
		l->add_theme_font_size_override("font_size", 13);
		l->add_theme_color_override("font_color", Color(0.85, 1, 0.85));
		l->add_theme_color_override("font_outline_color", Color(0, 0, 0));
		l->add_theme_constant_override("outline_size", 3);
		l->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		cl->add_child(l);
		add_child(cl);
		dbg_layer_ = cl; dbg_label_ = l;
	}
	if (CanvasLayer *cl = Object::cast_to<CanvasLayer>(dbg_layer_)) cl->set_visible(on);
	jvm::log("shotdbg", String("custom shot debugger ") + (on ? "ON" : "off") + " (" + OS::get_singleton()->get_keycode_string(dbg_key_) + ")");
	dbg_logged_state_ = "";
	if (on) dbg_update();
}

Node *JVExtraShots::dbg_player() {
	Node *best = nullptr;
	for (uint64_t id : hooked_) {
		Node *b = Object::cast_to<Node>(ObjectDB::get_instance(ObjectID(id)));
		if (!b) continue;
		Node *me = local_player_for(b);
		Node3D *m3 = Object::cast_to<Node3D>(me);
		if (m3 && m3->is_visible_in_tree()) best = me;
	}
	return best ? best : local_player();
}

void JVExtraShots::dbg_event(const String &line) {
	dbg_events_.push_front(line);
	while (int(dbg_events_.size()) > dbg_n_) dbg_events_.pop_back();
	if (dbg_on_ && dbg_log_) jvm::log("shotdbg", "touch " + line);
}

String JVExtraShots::dbg_text(String &state) {
	Input *in = Input::get_singleton();
	String t = String("JV SHOT DEBUGGER  (") + OS::get_singleton()->get_keycode_string(dbg_key_) + " hides)   court: " + court_mode() +
			"   shots: " + (follow_.applied ? String("ON") : String("BLOCKED - ") + jvm::gate_reason(this)) + "\n";
	String held;
	for (const char *a : { "move_forward", "move_back", "move_left", "move_right", "jump", "contact", "set", "dive", "serve", "sprint", "strafe" }) {
		if (InputMap::get_singleton()->has_action(a) && in->is_action_pressed(a)) held += String(held.is_empty() ? "" : "  ") + a + "[" + key_label(a) + "]";
	}
	t += "held: " + (held.is_empty() ? String("-") : held) + "\n";
	Vector2 raw = InputMap::get_singleton()->has_action("move_forward") ? in->get_vector("move_left", "move_right", "move_forward", "move_back") : Vector2();
	jvm::shots::FrameRaw dfr = frame_of(dbg_player(), raw);
	auto mi = jvm::shots::move_intent(dfr.x, dfr.y);
	String dir = mi.forward ? "FORWARD (topspin)" : (mi.back ? "BACK (Float)" : (mi.side ? (mi.side > 0 ? String("RIGHT (Curveball)") : String("LEFT (Curveball)")) : String("none")));
	t += "keys " + String(Variant(raw)) + " walk you " + String(Variant(Vector2(dfr.world.x, dfr.world.z))) + " = in your hitting frame " + String(Variant(Vector2(dfr.x, -dfr.y))) + " (right, toward net) -> air intent: " + dir + "\n";
	Node *me = dbg_player();
	String intent = "-", pstate = "no local player";
	if (me) {
		const bool air = me->has_method("grounded") && !bool(IO().call("player.input_state", me, "grounded"));
		const double since = now() - serving_seen_for(me);
		const bool serving = bool(IO().get("player.input_state", me, "serving"));
		String window = serving ? String("SERVING (ball in hands / toss up)") : (since <= 0.6 ? String("serve contact window ") + String::num(0.6 - since, 2) + " s left" : String("rally"));
		pstate = String(air ? "AIRBORNE" : "grounded") + "   " + window + "   ball: " + (bool(IO().get("player.input_state", me, "shared_ball_off")) ? "own ball" : "shared ball") +
				"   contact_state " + String(IO().get("player.input_state", me, "contact_state"));
		if (follow_.applied && backend_ == "jv_ball") {
			jvm::shots::TouchState ts = build_touch(me, 0.6, 3);
			String why; int idx = decide(me, ts, why);
			intent = idx >= 0 ? String(shots_[idx].id.c_str()) : (why.is_empty() ? String("game's own shot (no custom combo held") + (air ? "" : "; air shots need a jump") + ")" : String("BLOCKED: ") + why);
		} else intent = "shots are off here";
	}
	t += "player: " + pstate + "\n";
	t += "your touch now -> " + intent + "\n";
	t += "last: " + (last_detail_.is_empty() ? last_event_ : last_detail_) + "   (applied " + String::num_int64(applied_count_) + ", skipped " + String::num_int64(skipped_count_) + ")\n";
	t += "last touches (newest first):\n";
	for (auto &e : dbg_events_) t += "  " + e + "\n";
	state = "held " + (held.is_empty() ? String("-") : held) + " | " + dir + " | " + pstate.get_slice("contact_state", 0).strip_edges() + " | now -> " + intent;
	return t;
}

void JVExtraShots::dbg_update() {
	String state;
	String txt = dbg_text(state);
	if (Label *l = Object::cast_to<Label>(dbg_label_)) l->set_text(txt);

	String key = state;
	int w = key.find("window ");
	if (w >= 0) key = key.substr(0, w) + "window" + key.substr(key.find(" s left", w) + 7);
	if (dbg_log_ && key != dbg_logged_state_ && now() - dbg_log_at_ >= 0.1) {
		dbg_logged_state_ = key; dbg_log_at_ = now();
		jvm::log("shotdbg", state);
	}
}

String JVExtraShots::get_debug_text() { String st; return dbg_text(st); }
