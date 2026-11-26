#include "native_audio.hpp"

#include <godot_cpp/classes/audio_server.hpp>
#include <godot_cpp/classes/audio_stream.hpp>
#include <godot_cpp/classes/audio_stream_player.hpp>
#include <godot_cpp/classes/audio_stream_player3d.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/variant/callable.hpp>

namespace godot {
void JVMNativeAudio::_bind_methods() {
	ClassDB::bind_method(D_METHOD("on_finished", "handle"), &JVMNativeAudio::on_finished);
}
}

namespace jvm::native_audio {

using godot::Array;
using godot::AudioServer;
using godot::AudioStream;
using godot::AudioStreamPlayer;
using godot::AudioStreamPlayer3D;
using godot::Callable;
using godot::Dictionary;
using godot::DirAccess;
using godot::Engine;
using godot::Node;
using godot::Object;
using godot::ObjectDB;
using godot::ObjectID;
using godot::PackedStringArray;
using godot::Ref;
using godot::Resource;
using godot::ResourceLoader;
using godot::SceneTree;
using godot::String;
using godot::Variant;
using godot::Vector3;

namespace {

Session g_session;
ObjectID g_host;
std::unordered_map<int, ObjectID> g_players;
std::unordered_map<std::string, Ref<AudioStream>> g_streams;
int g_pack_handle = 0;

String to_g(const std::string &s) { return String::utf8(s.c_str(), (int)s.size()); }
std::string to_s(const String &s) { return std::string(s.utf8().get_data()); }

SceneTree *tree() {
	return Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop());
}

Node *root() {
	SceneTree *t = tree();
	return t ? t->get_root() : nullptr;
}

String strip_listing_suffix(const String &path) {
	String low = path.to_lower();
	if (low.ends_with(".import")) return path.substr(0, path.length() - 7);
	if (low.ends_with(".remap")) return path.substr(0, path.length() - 6);
	return path;
}

void walk_dir(const String &dir, std::vector<std::string> &out) {
	Ref<DirAccess> d = DirAccess::open(dir);
	if (d.is_null()) return;
	d->list_dir_begin();
	for (String name = d->get_next(); !name.is_empty(); name = d->get_next()) {
		if (name.begins_with(".")) continue;
		String path = dir.path_join(name);
		if (d->current_is_dir()) {
			walk_dir(path, out);
			continue;
		}
		if (!is_audio_filename(to_s(name))) continue;
		out.push_back(to_s(strip_listing_suffix(path)));
	}
	d->list_dir_end();
}

void ensure_index() {
	if (g_session.stats().index_built) return;
	std::vector<std::string> paths;
	for (int i = 0; i < kScanRootCount; i++)
		walk_dir(String(kScanRoots[i]), paths);
	g_session.build_index(paths);
}

float to_db(float linear) {
	if (linear <= 0.0001f) return -80.0f;
	return godot::Math::linear2db(godot::Math::clamp(linear, 0.0001f, 4.0f));
}

String pick_bus(const String &want, const std::string &fallback) {
	AudioServer *as = AudioServer::get_singleton();
	auto ok = [&](const String &n) { return as && !n.is_empty() && as->get_bus_index(n) != -1; };
	if (ok(want)) return want;
	String fb = to_g(fallback);
	if (ok(fb)) return fb;
	if (ok("sfx")) return "sfx";
	if (ok("Master")) return "Master";
	return want.is_empty() ? fb : want;
}

godot::JVMNativeAudio *host_node() {
	return Object::cast_to<godot::JVMNativeAudio>(ObjectDB::get_instance(g_host));
}

godot::JVMNativeAudio *ensure_host() {
	if (godot::JVMNativeAudio *h = host_node()) return h;
	Node *r = root();
	if (!r) return nullptr;
	godot::JVMNativeAudio *h = memnew(godot::JVMNativeAudio);
	h->set_name("JVM_NativeAudio");
	h->set_process_mode(Node::PROCESS_MODE_ALWAYS);
	r->add_child(h);
	g_host = h->get_instance_id();
	return h;
}

Node *player_of(int handle) {
	auto it = g_players.find(handle);
	if (it == g_players.end()) return nullptr;
	return Object::cast_to<Node>(ObjectDB::get_instance(it->second));
}

void release_player(int handle) {
	if (Node *n = player_of(handle)) {
		if (n->get_parent()) n->get_parent()->remove_child(n);
		n->queue_free();
	}
	g_players.erase(handle);
}

void drop_unused_stream(const std::string &path) {
	for (auto &kv : g_players) {
		if (const Voice *v = g_session.voice(kv.first))
			if (v->path == path) return;
	}
	g_streams.erase(path);
}

void drop_host_if_idle() {
	if (g_session.stats().started || !g_players.empty()) return;
	if (godot::JVMNativeAudio *h = host_node()) {
		if (h->get_parent()) h->get_parent()->remove_child(h);
		h->queue_free();
	}
	g_host = ObjectID();
	g_streams.clear();
}

Ref<AudioStream> load_stream(const std::string &path) {
	auto it = g_streams.find(path);
	if (it != g_streams.end()) return it->second;


	if (!native_audio_path_ok(path)) return {};
	ResourceLoader *rl = ResourceLoader::get_singleton();
	if (!rl) return {};
	String gpath = to_g(path);
	if (!rl->exists(gpath)) return {};
	Ref<AudioStream> stream = rl->load(gpath);
	if (stream.is_valid()) g_streams[path] = stream;
	return stream;
}

}

void replay_or_stop(int handle);

Session &session() { return g_session; }

PlayRequest play_request_from_dict(const Dictionary &opts, const String &id, bool loop) {
	PlayRequest r;
	r.id = id;
	r.loop = loop || bool(opts.get("loop", false));
	r.volume = (float)opts.get("volume", 1.0);
	r.bus = String(opts.get("bus", ""));
	Variant pos = opts.get("position", opts.get("pos", Variant()));
	if (pos.get_type() == Variant::VECTOR3) {
		r.has_position = true;
		r.position = pos;
	} else if (pos.get_type() == Variant::ARRAY) {
		Array a = pos;
		if (a.size() >= 3) {
			r.has_position = true;
			r.position = Vector3((float)a[0], (float)a[1], (float)a[2]);
		}
	}
	return r;
}

Array list_native() {
	ensure_index();
	Array out;
	for (const Entry &e : g_session.index()) {
		Dictionary d;
		d["id"] = to_g(e.id);
		d["path"] = to_g(e.path);
		d["category"] = to_g(e.category);
		d["bus"] = to_g(e.default_bus);
		out.push_back(d);
	}
	return out;
}

PackedStringArray list_buses() {
	PackedStringArray out;
	AudioServer *as = AudioServer::get_singleton();
	if (!as) return out;
	for (int i = 0; i < as->get_bus_count(); i++)
		out.push_back(as->get_bus_name(i));
	return out;
}

Dictionary status_dict() {
	Stats st = g_session.stats();
	Dictionary d;
	d["started"] = st.started;
	d["index_built"] = st.index_built;
	d["index_count"] = st.index_count;
	d["alias_count"] = st.alias_count;
	d["cached_streams"] = st.cached_streams;
	d["players"] = st.players;
	d["nodes"] = host_node() ? 1 : 0;
	d["allocations"] = st.allocations;
	d["pack_handle"] = g_pack_handle;
	d["cached_stream_refs"] = (int)g_streams.size();
	d["idle"] = !st.started && st.players == 0 && !host_node() && g_streams.empty();
	d["host"] = host_node() ? String(host_node()->get_path()) : String();
	d["beach_id"] = String(kBeachId);
	d["beach_path"] = String(kBeachPath);
	d["beach_alias"] = String(kBeachAlias);
	return d;
}

Dictionary info_dict(const String &id) {
	ensure_index();
	Dictionary d;
	const Entry *e = g_session.find(to_s(id));
	d["found"] = e != nullptr;
	if (!e) return d;
	d["id"] = to_g(e->id);
	d["path"] = to_g(e->path);
	d["category"] = to_g(e->category);
	d["bus"] = to_g(e->default_bus);
	return d;
}

int play_native(const PlayRequest &req) {
	if (req.id.is_empty()) return 0;
	ensure_index();
	PlayOpts o;
	o.loop = req.loop;
	o.volume = req.volume;
	o.bus = to_s(req.bus);
	o.has_position = req.has_position;
	if (req.has_position) {
		o.x = req.position.x;
		o.y = req.position.y;
		o.z = req.position.z;
	}
	int handle = g_session.play(to_s(req.id), o);
	if (!handle) return 0;
	const Voice *v = g_session.voice(handle);
	if (!v) {
		g_session.stop(handle);
		return 0;
	}
	Ref<AudioStream> stream = load_stream(v->path);
	if (stream.is_null()) {
		g_session.stop(handle);
		drop_host_if_idle();
		return 0;
	}
	godot::JVMNativeAudio *host = ensure_host();
	if (!host) {
		g_session.stop(handle);
		drop_unused_stream(v->path);
		return 0;
	}
	String bus = pick_bus(to_g(v->bus), v->bus);
	Node *player = nullptr;
	if (req.has_position) {
		AudioStreamPlayer3D *p = memnew(AudioStreamPlayer3D);
		p->set_stream(stream);
		p->set_bus(bus);
		p->set_volume_db(to_db(v->volume));
		p->set_position(req.position);
		host->add_child(p);
		p->connect("finished", Callable(host, "on_finished").bind(handle));
		p->play();
		player = p;
	} else {
		AudioStreamPlayer *p = memnew(AudioStreamPlayer);
		p->set_stream(stream);
		p->set_bus(bus);
		p->set_volume_db(to_db(v->volume));
		host->add_child(p);
		p->connect("finished", Callable(host, "on_finished").bind(handle));
		p->play();
		player = p;
	}
	g_players[handle] = player->get_instance_id();
	return handle;
}

int play_one_shot(const String &id, const Dictionary &opts) {
	return play_native(play_request_from_dict(opts, id, false));
}

int play_loop(const String &id, const Dictionary &opts) {
	return play_native(play_request_from_dict(opts, id, true));
}

bool stop_handle(int handle) {
	const Voice *v = g_session.voice(handle);
	std::string path = v ? v->path : std::string();
	if (g_pack_handle == handle) g_pack_handle = 0;
	release_player(handle);
	bool ok = g_session.stop(handle);
	if (!path.empty()) drop_unused_stream(path);
	drop_host_if_idle();
	return ok;
}

int stop_named(const String &id) {
	ensure_index();
	std::vector<int> hs;
	std::string key = normalize_key(to_s(id));
	const Entry *e = g_session.find(to_s(id));
	for (auto &kv : g_players) {
		if (const Voice *v = g_session.voice(kv.first)) {
			if (normalize_key(v->id) == key || (e && v->path == e->path))
				hs.push_back(kv.first);
		}
	}
	for (int h : hs) stop_handle(h);
	return (int)hs.size();
}

void stop_everything() {
	std::vector<int> hs;
	for (auto &kv : g_players) hs.push_back(kv.first);
	for (int h : hs) stop_handle(h);
	g_session.stop_all();
	g_pack_handle = 0;
	g_streams.clear();
	drop_host_if_idle();
}

bool set_voice_volume(int handle, float volume) {
	if (!g_session.set_volume(handle, volume)) return false;
	if (AudioStreamPlayer *p = Object::cast_to<AudioStreamPlayer>(player_of(handle)))
		p->set_volume_db(to_db(volume));
	else if (AudioStreamPlayer3D *p3 = Object::cast_to<AudioStreamPlayer3D>(player_of(handle)))
		p3->set_volume_db(to_db(volume));
	return true;
}

bool set_voice_bus(int handle, const String &bus) {
	const Voice *v = g_session.voice(handle);
	String use = pick_bus(bus, v ? v->bus : "sfx");
	if (!g_session.set_bus(handle, to_s(use))) return false;
	if (AudioStreamPlayer *p = Object::cast_to<AudioStreamPlayer>(player_of(handle)))
		p->set_bus(use);
	else if (AudioStreamPlayer3D *p3 = Object::cast_to<AudioStreamPlayer3D>(player_of(handle)))
		p3->set_bus(use);
	return true;
}

PackAudio pack_from_dict(const Dictionary &audio) {
	if (audio.is_empty()) return {};
	String ambience;
	String bus = String(audio.get("bus", ""));
	float volume = (float)audio.get("volume", 1.0);
	Variant av = audio.get("ambience", audio.get("id", audio.get("path", Variant())));
	if (av.get_type() == Variant::STRING) {
		ambience = av;
	} else if (av.get_type() == Variant::DICTIONARY) {
		Dictionary ad = av;
		ambience = String(ad.get("id", ad.get("path", ad.get("ambience", ""))));
		if (bus.is_empty()) bus = String(ad.get("bus", ""));
		if (ad.has("volume")) volume = (float)ad.get("volume", volume);
	}
	return make_pack_audio(true, to_s(ambience), to_s(bus), volume);
}

void clear_pack_audio() {
	if (g_pack_handle) {
		int h = g_pack_handle;
		g_pack_handle = 0;
		stop_handle(h);
	}
}

int apply_pack_audio(const PackAudio &pack) {
	clear_pack_audio();
	if (!pack.present || pack.ambience.empty()) return 0;
	PlayRequest r;
	r.id = to_g(pack.ambience);
	r.loop = true;
	r.volume = pack.volume;
	r.bus = to_g(pack.bus);
	int h = play_native(r);
	g_pack_handle = h;
	return h;
}

void apply_pack_audio_godot(const Dictionary &audio) {
	if (audio.is_empty()) {
		clear_pack_audio();
		return;
	}
	apply_pack_audio(pack_from_dict(audio));
}

void clear_pack_audio_godot() { clear_pack_audio(); }

void replay_or_stop(int handle) {
	const Voice *v = g_session.voice(handle);
	if (v && v->loop) {
		if (AudioStreamPlayer *p = Object::cast_to<AudioStreamPlayer>(player_of(handle))) {
			p->play();
			return;
		}
		if (AudioStreamPlayer3D *p3 = Object::cast_to<AudioStreamPlayer3D>(player_of(handle))) {
			p3->play();
			return;
		}
	}
	stop_handle(handle);
}

}

namespace godot {

void JVMNativeAudio::on_finished(int handle) {
	jvm::native_audio::replay_or_stop(handle);
}

}
