#include "native_audio_core.hpp"

#include <algorithm>
#include <cctype>

namespace jvm::native_audio {

static std::string to_lower_copy(std::string s) {
	for (char &c : s) c = (char)std::tolower((unsigned char)c);
	return s;
}

bool is_audio_filename(const std::string &name) {
	std::string n = to_lower_copy(name);

	for (const char *suf : { ".remap", ".import" }) {
		std::size_t sl = std::char_traits<char>::length(suf);
		if (n.size() > sl && n.compare(n.size() - sl, sl, suf) == 0)
			n.resize(n.size() - sl);
	}
	auto has = [&](const char *ext) {
		std::size_t nlen = n.size(), elen = std::char_traits<char>::length(ext);
		return nlen > elen && n.compare(nlen - elen, elen, ext) == 0;
	};
	return has(".wav") || has(".ogg") || has(".mp3");
}

std::string strip_extension(const std::string &name) {
	std::string n = name;
	for (const char *suf : { ".remap", ".import" }) {
		std::size_t sl = std::char_traits<char>::length(suf);
		if (n.size() > sl && to_lower_copy(n.substr(n.size() - sl)) == suf)
			n.resize(n.size() - sl);
	}
	std::size_t dot = n.find_last_of('.');
	std::size_t slash = n.find_last_of("/\\");
	if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
		n.resize(dot);
	return n;
}

std::string normalize_key(const std::string &s) {
	std::string o;
	o.reserve(s.size());
	for (char c : s) {
		if (c == '\\') o.push_back('/');
		else o.push_back((char)std::tolower((unsigned char)c));
	}

	std::size_t a = 0, b = o.size();
	while (a < b && (o[a] == ' ' || o[a] == '\t')) ++a;
	while (b > a && (o[b - 1] == ' ' || o[b - 1] == '\t')) --b;
	return o.substr(a, b - a);
}

std::string short_id_from_path(const std::string &path) {
	std::string p = strip_extension(path);

	std::string n = normalize_key(p);
	const char *pref[] = { "res://game/sfx/", "res://game/ost/", "game/sfx/", "game/ost/" };
	for (const char *pr : pref) {
		std::size_t pl = std::char_traits<char>::length(pr);
		if (n.compare(0, pl, pr) == 0) {

			if (std::string(pr).find("ost") != std::string::npos)
				return std::string("ost/") + n.substr(pl);
			return n.substr(pl);
		}
	}
	if (n.compare(0, 6, "res://") == 0) n = n.substr(6);
	return n;
}

std::string category_from_path(const std::string &path) {
	std::string id = short_id_from_path(path);
	std::size_t slash = id.find('/');
	if (slash == std::string::npos) return id.empty() ? "sfx" : id;
	std::string head = id.substr(0, slash);
	if (head == "ost") {
		std::size_t s2 = id.find('/', slash + 1);
		return s2 == std::string::npos ? id : id.substr(0, s2);
	}
	return head;
}

std::string default_bus_from_path(const std::string &path) {
	std::string cat = normalize_key(category_from_path(path));
	if (cat == "ambiance" || cat == "ambience") return "ambient";
	if (cat == "crowd") return "crowd";
	if (cat == "announcer") return "announcer";
	if (cat.compare(0, 3, "ost") == 0) return "music";
	return "sfx";
}

void add_known_aliases(std::unordered_map<std::string, std::string> &aliases,
	const std::vector<Entry> &index) {
	auto has_path = [&](const char *path) {
		std::string want = normalize_key(path);
		for (auto &e : index)
			if (normalize_key(e.path) == want) return true;
		return false;
	};
	auto add = [&](const char *alias, const char *path) {
		if (has_path(path)) aliases[normalize_key(alias)] = path;
	};
	add(kBeachAlias, kBeachPath);
	add("beach_waves", kBeachPath);
	add("beach_ambience", kBeachPath);
	add(kBeachId, kBeachPath);
	add(kParkAlias, kParkPath);
	add("park_ambience", kParkPath);
	add("park_ambiance", kParkPath);
	add("crowd_murmur", kAudiencePath);
	add("audience", kAudiencePath);
}

bool pack_audio_ref_ok(const std::string &ref) {
	if (ref.empty() || ref.size() > 160) return false;
	for (unsigned char c : ref)
		if (c < 0x20 || c == 0x7f || c == '\\' || c == '%' || c == '"' || c == '*' || c == '?' || c == '<' || c == '>' || c == '|') return false;
	if (ref.find("..") != std::string::npos) return false;
	std::size_t colon = ref.find(':');
	if (colon == std::string::npos) return ref[0] != '/' && ref[0] != '~';
	return native_audio_path_ok(ref);
}

bool native_audio_path_ok(const std::string &path) {
	if (path.size() > 200 || path.find("..") != std::string::npos || path.find('\\') != std::string::npos) return false;
	if (path.find("//", 6) != std::string::npos) return false;
	if (!(path.compare(0, 15, "res://game/sfx/") == 0 || path.compare(0, 15, "res://game/ost/") == 0)) return false;
	if (path.find(':', 4) != std::string::npos) return false;
	std::string low = to_lower_copy(path);
	for (const char *ext : { ".wav", ".ogg", ".mp3" }) {
		std::size_t el = std::char_traits<char>::length(ext);
		if (low.size() > el && low.compare(low.size() - el, el, ext) == 0) return true;
	}
	return false;
}

PackAudio make_pack_audio(bool has_block, const std::string &ambience,
	const std::string &bus, float volume) {
	PackAudio p;
	if (!has_block) return p;

	p.ambience = pack_audio_ref_ok(ambience) ? ambience : std::string();
	p.bus = bus.size() <= 64 ? bus : std::string();
	p.volume = (volume == volume && volume > 0.0f) ? (volume < 4.0f ? volume : 4.0f) : 0.0f;
	p.present = !p.ambience.empty();
	return p;
}

void Session::account(int n) {
	if (n > 0) stats_.allocations += n;
}

void Session::build_index(const std::vector<std::string> &paths) {
	index_.clear();
	by_key_.clear();
	stats_.index_built = true;
	stats_.index_count = 0;
	stats_.alias_count = 0;



	std::unordered_map<std::string, std::size_t> seen;
	for (const std::string &raw : paths) {
		if (raw.empty() || !is_audio_filename(raw)) continue;
		std::string path = raw;

		if (path.compare(0, 6, "res://") != 0 && path.compare(0, 5, "res:/") != 0) {
			if (path.compare(0, 5, "game/") == 0) path = "res://" + path;
		}
		std::string pkey = normalize_key(path);
		if (seen.count(pkey)) continue;
		Entry e;
		e.path = path;
		e.id = short_id_from_path(path);
		e.category = category_from_path(path);
		e.default_bus = default_bus_from_path(path);
		std::size_t slot = index_.size();
		index_.push_back(e);
		account(1);
		seen[pkey] = slot;
		by_key_[normalize_key(e.id)] = slot;
		by_key_[pkey] = slot;

		by_key_[normalize_key(strip_extension(path))] = slot;
		if (e.path.compare(0, 6, "res://") == 0)
			by_key_[normalize_key(e.path.substr(6))] = slot;
	}
	std::unordered_map<std::string, std::string> aliases;
	add_known_aliases(aliases, index_);
	for (auto &kv : aliases) {
		auto it = seen.find(normalize_key(kv.second));
		if (it == seen.end()) continue;
		if (by_key_.emplace(kv.first, it->second).second) {
			stats_.alias_count++;
			account(1);
		}
	}
	stats_.index_count = (int)index_.size();
}

const Entry *Session::find(const std::string &id_or_path) const {
	auto it = by_key_.find(normalize_key(id_or_path));
	if (it == by_key_.end()) return nullptr;
	if (it->second >= index_.size()) return nullptr;
	return &index_[it->second];
}

std::string Session::resolve_path(const std::string &id_or_path) const {
	if (const Entry *e = find(id_or_path)) return e->path;
	return {};
}

void Session::ensure_started() {
	if (stats_.started) return;
	stats_.started = true;
	stats_.nodes = 1;
	account(1);
}

void Session::maybe_idle() {
	if (!voices_.empty()) return;

	if (stats_.nodes) {
		stats_.nodes = 0;
	}
	stats_.started = false;
	stats_.players = 0;
	stats_.cached_streams = (int)stream_users_.size();
}

void Session::drop_stream(const std::string &path) {
	auto it = stream_users_.find(path);
	if (it == stream_users_.end()) return;
	if (--it->second > 0) return;
	stream_users_.erase(it);
	stats_.cached_streams = (int)stream_users_.size();
}

int Session::play(const std::string &id, const PlayOpts &opts) {
	const Entry *e = find(id);
	if (!e) return 0;
	ensure_started();
	if (!stream_users_.count(e->path)) {
		stream_users_[e->path] = 0;
		account(1);
	}
	stream_users_[e->path]++;
	stats_.cached_streams = (int)stream_users_.size();
	Voice v;
	v.handle = next_handle_++;
	v.id = e->id;
	v.path = e->path;
	v.loop = opts.loop;
	v.volume = opts.volume;
	v.bus = opts.bus.empty() ? e->default_bus : opts.bus;
	v.spatial = opts.has_position;
	voices_[v.handle] = v;
	account(1);
	stats_.players = (int)voices_.size();
	return v.handle;
}

Voice *Session::mutable_voice(int handle) {
	auto it = voices_.find(handle);
	return it == voices_.end() ? nullptr : &it->second;
}

const Voice *Session::voice(int handle) const {
	auto it = voices_.find(handle);
	return it == voices_.end() ? nullptr : &it->second;
}

bool Session::stop(int handle) {
	auto it = voices_.find(handle);
	if (it == voices_.end()) return false;
	if (pack_handle_ == handle) pack_handle_ = 0;
	drop_stream(it->second.path);
	voices_.erase(it);
	stats_.players = (int)voices_.size();
	maybe_idle();
	return true;
}

int Session::stop_id(const std::string &id) {
	const Entry *e = find(id);
	std::string path = e ? e->path : resolve_path(id);
	std::string key = normalize_key(id);
	std::vector<int> hs;
	for (auto &kv : voices_) {
		if (normalize_key(kv.second.id) == key || kv.second.path == path
			|| (e && kv.second.path == e->path))
			hs.push_back(kv.first);
	}
	for (int h : hs) stop(h);
	return (int)hs.size();
}

void Session::stop_all() {
	std::vector<int> hs;
	for (auto &kv : voices_) hs.push_back(kv.first);
	for (int h : hs) stop(h);
}

bool Session::set_volume(int handle, float vol) {
	Voice *v = mutable_voice(handle);
	if (!v) return false;
	v->volume = vol;
	return true;
}

bool Session::set_bus(int handle, const std::string &bus) {
	Voice *v = mutable_voice(handle);
	if (!v) return false;
	if (!bus.empty()) v->bus = bus;
	return true;
}

int Session::apply_pack(const PackAudio &pack) {
	clear_pack();
	if (!pack.present || pack.ambience.empty()) return 0;
	PlayOpts o;
	o.loop = true;
	o.volume = pack.volume;
	o.bus = pack.bus;
	int h = play(pack.ambience, o);
	pack_handle_ = h;
	return h;
}

void Session::clear_pack() {
	if (pack_handle_) stop(pack_handle_);
	pack_handle_ = 0;
}

}
