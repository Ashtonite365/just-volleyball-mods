



















#include "goofy_mods.hpp"
#include "../common/jv_prof.hpp"

#include <cmath>

#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/multiplayer_api.hpp>
#include <godot_cpp/classes/multiplayer_peer.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;
namespace G = jvm::goofy;

jvm::GameIO &jvm_goofy_io();
static jvm::GameIO &IO() { return jvm_goofy_io(); }
static const char *kMod = "goofy";

static const char kHello[] = "JVGH";
static const char kFull[] = "JVGF";
static constexpr double kHeartbeat = 5.0, kHelloEvery = 2.0, kHelloKeepAlive = 5.0, kNetPoll = 0.25;

static bool has_header(const PackedByteArray &b, const char *h) {
	if (b.size() < 4) return false;
	for (int i = 0; i < 4; i++) if (b[i] != uint8_t(h[i])) return false;
	return true;
}
static PackedByteArray frame(const char *h, const String &json) {
	PackedByteArray out;
	for (int i = 0; i < 4; i++) out.push_back(uint8_t(h[i]));
	out.append_array(json.to_utf8_buffer());
	return out;
}

Object *JVGoofyMods::sync_mp() {
	if (!is_inside_tree()) return nullptr;
	Ref<MultiplayerAPI> mp = get_tree()->get_multiplayer();
	return mp.is_valid() ? mp.ptr() : nullptr;
}

void JVGoofyMods::set_sync_role(const String &role) {
	const String prev = sync_role_;
	if (prev == "client" && role != "client") { apply_host_gameplay(false); stack_.clear_host_values(); }
	sync_role_ = role;
	if (prev != role) { pc_.clear_pending(); pc_peer_ids_.clear(); }




	if (prev != role) { follow_dirty_ = true; gate_dirty_ = true; panel_dirty_ = true; }
	const bool client = role == "client";
	recv_seq_ = -1; recv_sid_ = 0;
	host_gone_ = false; host_in_match_ = false; net_poll_ = 1e9;
	if (!reloading_) sync_peers_.clear();
	sync_hello_ = 1e9;
	if (role == "host") { sync_sid_ = int64_t(UtilityFunctions::randi() & 0x3fffffff) + 1; sync_seq_ = 0; host_dirty_ = true; }
	jvm::log("goofy", "sync: " + prev + " -> " + role + (client ? String(" (host gameplay values from match start to match end)") : role == "host" ? String(" (publishing gameplay values to modded peers)") : String()));

	if ((role == "host" || client)) {
		Object *mp = sync_mp();
		if (mp && (!sync_hooked_ || sync_mp_id_ != mp->get_instance_id())) {
			sync_hooked_ = IO().connect("net.packets", mp, "peer_packet", Callable(this, "_on_sync_packet")) == OK;
			IO().connect("net.packets", mp, "peer_disconnected", Callable(this, "_on_sync_peer_gone"));
			sync_mp_id_ = mp->get_instance_id();
		}
	}
	if (physics_allowed_) push_library();
	emit_signal("sync_changed", role, int64_t(stack_.host_values().size()));
}




Dictionary JVGoofyMods::gameplay_snapshot() const {
	Dictionary params;
	Array k = pushed_.keys();
	for (int i = 0; i < k.size(); i++) {
		const G::Param *p = G::find(String(k[i]).utf8().get_data());
		if (!p || G::is_visual(*p)) continue;
		params[k[i]] = double(pushed_[k[i]]);
	}
	Dictionary d;
	d["params"] = params;
	d["ts"] = gs_registered_ ? gs_time_scale_ : (time_changed_ ? cfg_ts_ : 1.0);
	return d;
}

PackedByteArray JVGoofyMods::host_payload() {
	Dictionary snap = gameplay_snapshot();
	Dictionary d;
	d["v"] = G::kHostPayloadVersion;
	d["sid"] = sync_sid_;
	d["seq"] = ++sync_seq_;
	d["ts"] = snap["ts"];
	d["params"] = snap["params"];
	return frame(kFull, JSON::stringify(d, "", true, true));
}

void JVGoofyMods::sync_send(int64_t peer, const PackedByteArray &bytes) {
	Object *mp = sync_mp();
	if (!mp) return;
	IO().call("net.packets", mp, "send_bytes", bytes, peer, int64_t(MultiplayerPeer::TRANSFER_MODE_RELIABLE), int64_t(0));
	sync_sent_++;
}


void JVGoofyMods::apply_host_gameplay(bool on) {
	if (on == host_live_) return;
	host_live_ = on;
	stack_.set_host_mode(on);
	Object *a = api();
	if (on) {
		if (a) {
			ts_client_orig_ = a->call("time_scale_get");
			ts_synced_ = bool(a->call("time_scale_set", kMod, ts_host_));
		}
		jvm::log("goofy", "sync: match started - host gameplay values in force (" + String::num_int64(stack_.host_values().size()) + " param(s), time scale " + String::num(ts_host_) + ")");
	} else {
		if (ts_synced_ && a) a->call("time_scale_restore", kMod, ts_client_orig_);
		ts_synced_ = false;
		jvm::log("goofy", "sync: own values restored (match ended / host left)");
	}
	if (physics_allowed_) push_library();
	emit_signal("sync_changed", sync_role_, int64_t(stack_.host_values().size()));
}


void JVGoofyMods::session_tick(double delta) {
	JVM_PROF("goofy.net.session_tick");
	if (sync_role_ != "host" && sync_role_ != "client") return;
	net_poll_ += delta;
	if (net_poll_ < kNetPoll) return;
	net_poll_ = 0.0;
	Object *a = api();
	if (!a) return;
	Dictionary net = a->call("get_net_state", kMod);
	const bool live = bool(net.get("in_match", false)) || bool(net.get("practice_live", false));
	if (sync_role_ == "host") {
		if (live && !host_in_match_) { host_dirty_ = true; sync_hb_ = 0.0; jvm::log("goofy", "sync: match started - broadcasting gameplay values"); }
		host_in_match_ = live;
		return;
	}
	if (!live) host_gone_ = false;
	apply_host_gameplay(live && !host_gone_);
}




void JVGoofyMods::time_scale_tick() {
	const double own = gs_registered_ ? gs_time_scale_ : (time_changed_ ? cfg_ts_ : 1.0);
	const double want = ts_synced_ ? ts_host_ : own;
	if (want == 1.0) return;
	Object *a = api();
	if (a && double(a->call("time_scale_get")) == 1.0) a->call("time_scale_set", kMod, want);
}

void JVGoofyMods::host_sync_tick(double delta) {
	JVM_PROF("goofy.net.host_sync_tick");
	session_tick(delta);
	if (follow_.applied) time_scale_tick();
	if (sync_role_ == "client") {
		sync_hello_ += delta;
		if (sync_hello_ < (recv_seq_ >= 0 ? kHelloKeepAlive : kHelloEvery)) return;
		sync_hello_ = 0.0;

		const String pc = pc_manifest_id();
		sync_send(1, frame(kHello, pc.is_empty() ? String("{\"v\":2}") : "{\"v\":2,\"pc\":" + JSON::stringify(pc) + "}"));
		return;
	}
	if (sync_role_ != "host" || sync_peers_.empty()) return;
	sync_hb_ += delta;
	if (!host_dirty_ && sync_hb_ < kHeartbeat) return;
	sync_hb_ = 0.0;
	host_dirty_ = false;
	PackedByteArray b = host_payload();
	for (int64_t p : sync_peers_) sync_send(p, b);
}

void JVGoofyMods::_on_sync_peer_gone(int64_t id) {
	sync_peers_.erase(id);
	pc_peer_ids_.erase(id);
	if (id == 1 && sync_role_ == "client") {
		host_gone_ = true;
		apply_host_gameplay(false);
		stack_.clear_host_values();
		recv_seq_ = -1; recv_sid_ = 0;
	}
}

void JVGoofyMods::_on_sync_packet(int64_t from, const PackedByteArray &packet) {
	const bool hello = has_header(packet, kHello), full = has_header(packet, kFull), plan = has_header(packet, "JVPC");
	if (!hello && !full && !plan) return;
	if (!follow_.applied) return;
	auto reject = [&](const String &why) { sync_rejected_++; sync_last_error_ = why; jvm::warn("goofy", "sync: rejected packet from " + String::num_int64(from) + ": " + why); };
	if (packet.size() > G::kHostPayloadMaxBytes) { reject("too large"); return; }
	if (plan) { pc_on_plan(from, packet); return; }
	if (hello) {
		if (sync_role_ != "host") return;
		{
			Variant hv = JSON::parse_string(packet.slice(4).get_string_from_utf8());
			String pc;
			if (hv.get_type() == Variant::DICTIONARY && Dictionary(hv).get("pc", Variant()).get_type() == Variant::STRING) pc = String(Dictionary(hv)["pc"]).left(64);
			pc_peer_ids_[from] = pc;
		}
		if (!sync_peers_.insert(from).second) return;
		jvm::log("goofy", "sync: modded peer " + String::num_int64(from) + " joined");
		sync_send(from, host_payload());
		return;
	}
	if (sync_role_ != "client") return;
	if (from != 1) { reject("not from the host"); return; }
	Variant parsed = JSON::parse_string(packet.slice(4).get_string_from_utf8());
	if (parsed.get_type() != Variant::DICTIONARY) { reject("not a JSON object"); return; }
	Dictionary d = parsed;
	auto num = [](const Variant &v, double &out) {
		if (v.get_type() != Variant::INT && v.get_type() != Variant::FLOAT) return false;
		out = double(v);
		return std::isfinite(out);
	};
	double ver = 0, sid = 0, seq = 0, ts_in = 1.0, ts = 1.0;
	if (!num(d.get("v", Variant()), ver) || ver != double(G::kHostPayloadVersion)) { reject("unsupported version"); return; }
	if (!num(d.get("sid", Variant()), sid) || !num(d.get("seq", Variant()), seq) || seq < 0 || seq > 9e15) { reject("bad sid/seq"); return; }
	if (d.get("params", Variant()).get_type() != Variant::DICTIONARY) { reject("params must be an object"); return; }
	if (!num(d.get("ts", Variant()), ts_in) || !G::sanitize_time_scale(ts_in, ts)) { reject("bad time scale"); return; }
	if (int64_t(sid) == recv_sid_ && int64_t(seq) <= recv_seq_) return;
	Dictionary params = d["params"];
	if (params.size() > int(G::params().size())) { reject("too many params"); return; }
	std::map<std::string, G::Value> vals;
	int bad = 0;
	Array keys = params.keys();
	for (int i = 0; i < keys.size(); i++) {
		if (keys[i].get_type() != Variant::STRING) { bad++; continue; }
		const G::Param *p = G::find(String(keys[i]).utf8().get_data());
		if (!p) { bad++; continue; }
		Variant v = params[keys[i]];
		double in[4] = { 0, 0, 0, 1 };
		int n = 0;
		if (v.get_type() == Variant::ARRAY) {
			Array a = v;
			if (a.size() > 4) { bad++; continue; }
			bool ok = true;
			for (int j = 0; j < a.size(); j++) ok = ok && num(a[j], in[j]);
			if (!ok) { bad++; continue; }
			n = a.size();
		} else if (num(v, in[0])) n = 1;
		else { bad++; continue; }
		G::Value out;
		if (!G::sanitize_host_value(*p, in, n, out)) { bad++; continue; }
		vals[p->id] = out;
	}
	if (bad) { sync_rejected_ += bad; sync_last_error_ = String::num_int64(bad) + " value(s) dropped"; jvm::warn("goofy", "sync: dropped " + String::num_int64(bad) + " invalid value(s) from the host"); }
	recv_sid_ = int64_t(sid); recv_seq_ = int64_t(seq);
	sync_recv_++;
	const bool first = sync_applied_ == 0;
	stack_.set_host_values(std::move(vals));
	sync_applied_++;
	if (first) jvm::log("goofy", "sync: host values received (" + String::num_int64(stack_.host_values().size()) + " param(s), time scale " + String::num(ts) + ")");
	if (ts != ts_host_) {
		ts_host_ = ts;
		if (ts_synced_) if (Object *a = api()) a->call("time_scale_set", kMod, ts_host_);
	}
	if (host_live_) push_library();
	emit_signal("sync_changed", sync_role_, int64_t(stack_.host_values().size()));
}

Dictionary JVGoofyMods::get_sync_status() const {
	Dictionary d;
	d["role"] = sync_role_;
	d["authoritative"] = sync_role_ == "client" ? (host_live_ ? "host" : "local") : (sync_role_ == "off" ? "none" : "local");
	d["live"] = sync_role_ == "client" ? host_live_ : host_in_match_;
	d["host_left"] = host_gone_;
	d["host_values"] = int64_t(stack_.host_values().size());
	d["time_scale"] = sync_role_ == "client" ? ts_host_ : double(gameplay_snapshot()["ts"]);
	d["time_scale_synced"] = ts_synced_;
	d["peers"] = int64_t(sync_peers_.size());
	d["sent"] = sync_sent_; d["received"] = sync_recv_; d["rejected"] = sync_rejected_;
	d["seq"] = sync_role_ == "host" ? sync_seq_ : recv_seq_;
	d["last_error"] = sync_last_error_;
	d["version"] = G::kHostPayloadVersion;
	return d;
}
