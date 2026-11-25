









#include "goofy_mods.hpp"

#include <godot_cpp/classes/class_db_singleton.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/multiplayer_api.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;
namespace PC = jvm::pconsts;

jvm::GameIO &jvm_goofy_io();
static jvm::GameIO &IO() { return jvm_goofy_io(); }
static const char kPlan[] = "JVPC";
static const char *kCap = "player.consts";
static String S(const std::string &s) { return String::utf8(s.c_str()); }
static std::string U(const String &s) { return std::string(s.utf8().get_data()); }
static double pc_now() { return double(Time::get_singleton()->get_ticks_msec()) / 1000.0; }

bool JVGoofyMods::pc_available() {
	if (!pc_checked_) {
		Variant keep = ClassDBSingleton::get_singleton()->class_exists("JVPlayerConsts") ? ClassDBSingleton::get_singleton()->instantiate("JVPlayerConsts") : Variant();
		Object *o = keep.get_type() == Variant::OBJECT ? keep.operator Object *() : nullptr;
		pc_verify_ = o ? Dictionary(o->call("check")) : Dictionary();
		if (!o) pc_verify_["reason"] = "CORE without player.consts";
		pc_checked_ = true;
		jvm::log("goofy", String("player.consts: ") + (bool(pc_verify_.get("available", false)) ? "available" : "unavailable") + " (" + String(pc_verify_.get("reason", "")) + ")");
	}
	return bool(pc_verify_.get("available", false));
}
String JVGoofyMods::pc_manifest_id() { return pc_available() ? S(PC::manifest_id(U(String(pc_verify_.get("pin", ""))))) : String(); }

void JVGoofyMods::pc_write(const std::map<std::string, double> &vals, bool undo) {
	if (!pc_available()) return;
	Ref<Script> sc = ResourceLoader::get_singleton()->load("res://game/player/player.gd");
	if (sc.is_null()) return;
	for (const auto &kv : vals) {
		const PC::Spec *sp = PC::find(kv.first);
		Variant v = sp && sp->is_int ? Variant(int64_t(kv.second)) : Variant(kv.second);
		IO().set(kCap, sc.ptr(), StringName(kv.first.c_str()), v, undo);
	}
}

void JVGoofyMods::pc_load_config() {
	if (!pc_.locked()) for (const auto &sp : PC::specs()) pc_.reset(sp.name, "config");
	Dictionary d = cfg_.get("player_consts", Dictionary());
	Array k = d.keys();
	for (int i = 0; i < k.size(); i++) {
		Variant v = d[k[i]];
		if (v.get_type() != Variant::INT && v.get_type() != Variant::FLOAT) continue;
		PC::Result r = pc_.set(U(String(k[i])), double(v), "config");
		if (!r.ok) jvm::warn("goofy", S(r.error));
	}
}

String JVGoofyMods::pc_peer_name(int64_t peer) {
	Node *net = IO().node("net.state", this, "/root/Net");
	if (net) {
		Variant pl = IO().get("net.state", net, "players");
		if (pl.get_type() == Variant::DICTIONARY && Dictionary(pl).has(peer)) {
			Variant e = Dictionary(pl)[peer];
			if (e.get_type() == Variant::DICTIONARY && Dictionary(e).has("name")) return String(Dictionary(e)["name"]);
		}
	}
	return "peer " + String::num_int64(peer);
}

void JVGoofyMods::pc_match_start() {
	pc_in_match_ = true;
	pc_notice_ = "";
	const uint64_t mid = (uint64_t(UtilityFunctions::randi()) << 20) ^ ++pc_match_seq_;
	PC::Context ctx = sync_role_ == "host" ? PC::Context::Host : sync_role_ == "client" ? PC::Context::Client :
			sync_role_ == "offline" ? PC::Context::Offline : PC::Context::Public;
	if (!pc_available() && ctx != PC::Context::Public) ctx = PC::Context::Public;
	if (ctx == PC::Context::Client) { pc_.begin_match_client(mid, pc_now()); pc_write(pc_.values(), true); return; }
	std::vector<std::string> players;
	std::map<std::string, int64_t> by_name;
	if (ctx == PC::Context::Host) {
		if (Object *mp = sync_mp()) {
			PackedInt32Array peers = Object::cast_to<MultiplayerAPI>(mp)->get_peers();
			for (int i = 0; i < peers.size(); i++) {
				std::string n = U(pc_peer_name(peers[i]));
				if (by_name.count(n)) n += " #" + std::to_string(peers[i]);
				players.push_back(n); by_name[n] = peers[i];
			}
		}
	}
	const String mine = pc_manifest_id();



	Object *api_obj = api();
	const bool shared = api_obj && api_obj->has_method("gameplay_mod_active_for") && api_obj->has_method("gameplay_player_mods") &&
			!Dictionary(api_obj->call("gameplay_player_mods", "goofy")).is_empty();
	PC::ActiveFn active = [&](const std::string &p, std::string *why) {
		auto it = by_name.find(p);
		if (it == by_name.end()) { if (why) *why = "missing"; return false; }

		if (shared && !bool(api_obj->call("gameplay_mod_active_for", "goofy", "goofy", String::num_int64(it->second)))) { if (why) *why = "missing"; return false; }

		auto id = pc_peer_ids_.find(it->second);
		if (id == pc_peer_ids_.end() || id->second.is_empty()) { if (why) *why = "missing"; return false; }
		if (id->second != mine) { if (why) *why = U("other game build: " + id->second.get_slice("@", 1) + ", host " + mine.get_slice("@", 1)); return false; }
		return true;
	};
	PC::Plan plan = pc_.begin_match_host(mid, ctx, players, active);
	pc_write(pc_.values(), false);
	if (ctx == PC::Context::Host) {
		PackedByteArray b;
		for (int i = 0; i < 4; i++) b.push_back(uint8_t(kPlan[i]));
		b.append_array(S(PC::encode_plan(plan)).to_utf8_buffer());
		for (int64_t p : sync_peers_) sync_send(p, b);
	}
	if (!plan.notice.empty()) {
		pc_notice_ = S(plan.notice);
		jvm::warn("goofy", pc_notice_);
		emit_signal("player_consts_notice", pc_notice_);
		if (Object *a = api()) if (a->has_method("notify")) a->call("notify", "goofy", pc_notice_);
	}
	jvm::log("goofy", String("player.consts: match start (") + sync_role_ + ") -> " + (plan.apply && !plan.values.empty() ? String::num_int64(plan.values.size()) + " edited value(s) for everyone" : String("stock")));
}

void JVGoofyMods::pc_match_end() {
	pc_in_match_ = false;
	pc_write(pc_.end_match(), true);
	jvm::log("goofy", "player.consts: match over -> stock, edits unlocked");
}

void JVGoofyMods::pc_tick() {
	Node *sc = jvm::current_scene(this);
	bool in_match = false;
	if (sc) {
		Variant r = IO().get("scene.rally", sc, "_rally");
		Variant emb = IO().get("scene.rally", sc, "embedded");
		Object *ro = r.get_type() == Variant::OBJECT ? r.operator Object *() : nullptr;
		in_match = ro != nullptr && !(emb.get_type() == Variant::BOOL && bool(emb));
	}
	if (in_match && !pc_in_match_) pc_match_start();
	else if (!in_match && pc_in_match_) pc_match_end();
}

void JVGoofyMods::pc_on_plan(int64_t from, const PackedByteArray &packet) {
	if (sync_role_ != "client") return;
	PC::Result r = pc_.on_plan(U(packet.slice(4).get_string_from_utf8()), from == 1, pc_now());
	if (!r.ok) { sync_rejected_++; sync_last_error_ = S(r.error); jvm::warn("goofy", S(r.error)); return; }
	if (pc_.locked()) pc_write(pc_.values(), true);
	jvm::log("goofy", String("player.consts: host's match set received (") + (pc_.plan().apply ? String::num_int64(pc_.plan().values.size()) + " edited" : String("stock")) + ")");
}

Dictionary JVGoofyMods::player_consts_set(const String &name, double value, const String &source) {
	PC::Result r = pc_.set(U(name), value, U(source));
	if (!r.ok) jvm::warn("goofy", S(r.error));
	Dictionary d; d["ok"] = r.ok; d["error"] = S(r.error);
	return d;
}
Dictionary JVGoofyMods::player_consts_reset(const String &name, const String &source) {
	PC::Result r;
	if (name == "*") { for (const auto &sp : PC::specs()) { r = pc_.reset(sp.name, U(source)); if (!r.ok) break; } }
	else r = pc_.reset(U(name), U(source));
	if (!r.ok) jvm::warn("goofy", S(r.error));
	Dictionary d; d["ok"] = r.ok; d["error"] = S(r.error);
	return d;
}
Dictionary JVGoofyMods::player_consts_get() {
	Dictionary d, saved, play, stock;
	for (auto &kv : pc_.saved()) saved[S(kv.first)] = kv.second;
	for (auto &kv : pc_.values()) play[S(kv.first)] = kv.second;
	for (auto &s : PC::specs()) stock[s.name] = s.stock;
	d["available"] = pc_available();
	d["reason"] = pc_verify_.get("reason", "");
	d["manifest_id"] = pc_manifest_id();
	d["locked"] = pc_.locked();
	d["saved"] = saved; d["in_play"] = play; d["stock"] = stock;
	d["applied"] = pc_.locked() && pc_.plan().apply;
	d["notice"] = pc_notice_;
	d["plan_received"] = pc_.locked() && pc_.plan_received();
	d["rejected"] = int64_t(pc_.rejected());
	Array w; for (auto &x : pc_.warnings()) w.push_back(S(x));
	d["warnings"] = w;
	return d;
}
