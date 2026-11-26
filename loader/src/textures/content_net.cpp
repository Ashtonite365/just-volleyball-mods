










#include "texture_loader.hpp"
#include "safe_load.hpp"
#include "gate/safe_content.hpp"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/http_client.hpp>
#include <godot_cpp/classes/http_request.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/multiplayer_peer.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/scene_multiplayer.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/zip_reader.hpp>

#include <algorithm>
#include <cstring>

using namespace godot;
namespace sk = jvm::skins;
namespace cs = jvm::content;

static std::string S(const String &s) { return std::string(s.utf8().get_data()); }
static String G(const std::string &s) { return String::utf8(s.c_str()); }
static bool sync_open() { godot::Object *a = jvm_textures_io().api(); return a && bool(a->call("is_open", "sync")); }
static double now_s() { return Time::get_singleton()->get_ticks_msec() / 1000.0; }
static String hex_id(uint16_t id) { return G(sk::format_id(id)); }

String godot::content_cache_dir() { return jvm::mods_dir("resources/cache"); }
String godot::content_packs_dir() { return jvm::mods_dir("resources/packs"); }

String JVTextureLoader::content_base() const {


	OS *os = OS::get_singleton();
	if (os->is_debug_build()) {
		String b = os->get_environment("JV_ASSETS_BASE");
		if (b.begins_with("http://127.0.0.1:") && b.ends_with("/") && b.length() < 64) return b;
	}
	return cs::kRawBase;
}
bool JVTextureLoader::content_auto_download() const { return bool(sync_cfg_.get("content_auto_download", true)); }


struct FileHashCache { int64_t mtime = -1, size = -1; cs::Hash h{}; };
static std::map<String, FileHashCache> g_file_hash;

static void list_files(const String &base, const String &rel, int depth, PackedStringArray &out) {
	if (depth > 3 || out.size() > int64_t(cs::kMaxEntries) * 2) return;
	String dir = rel.is_empty() ? base : base.path_join(rel);
	PackedStringArray f = DirAccess::get_files_at(dir);
	for (int i = 0; i < f.size(); i++) out.push_back(rel.is_empty() ? f[i] : rel + String("/") + f[i]);
	PackedStringArray d = DirAccess::get_directories_at(dir);
	for (int i = 0; i < d.size(); i++) list_files(base, rel.is_empty() ? d[i] : rel + String("/") + d[i], depth + 1, out);
}

bool JVTextureLoader::pack_item(const String &dir, cs::Kind kind, uint16_t id, cs::Item &out) {
	if (dir.is_empty() || !DirAccess::dir_exists_absolute(dir)) return false;
	PackedStringArray files;
	list_files(dir, "", 1, files);
	std::vector<cs::FileEntry> entries;
	for (int i = 0; i < files.size(); i++) {
		auto safe = cs::safe_entry(S(files[i]));
		if (!safe || safe->empty()) continue;
		String full = dir.path_join(files[i]);
		int64_t mt = int64_t(FileAccess::get_modified_time(full));
		Ref<FileAccess> fa = FileAccess::open(full, FileAccess::READ);
		if (fa.is_null()) continue;
		int64_t sz = int64_t(fa->get_length());
		FileHashCache &c = g_file_hash[full];
		if (c.mtime != mt || c.size != sz) {
			if (sz > int64_t(cs::kMaxFileBytes)) continue;
			PackedByteArray b = fa->get_buffer(sz);
			c.h = cs::sha256(b.ptr(), size_t(b.size()));
			c.mtime = mt; c.size = sz;
		}
		entries.push_back({ *safe, c.h, uint64_t(sz) });
	}
	if (entries.empty()) return false;
	uint64_t total = cs::tree_size(entries);
	if (total > 0xFFFFFFFFull) return false;
	out.id = id; out.kind = kind; out.hash = cs::tree_hash(entries); out.size = uint32_t(total);
	return true;
}

std::map<uint16_t, cs::Hash> JVTextureLoader::installed_hashes() {
	std::map<uint16_t, cs::Hash> m;
	for (auto &[id, ref] : pack_refs_) { cs::Item it; if (pack_item(ref.dir, cs::Kind::Outfit, id, it)) m[id] = it.hash; }
	Dictionary md = jvm_models_installed_dirs();
	Array keys = md.keys();
	for (int i = 0; i < keys.size(); i++) {
		auto id = sk::parse_id(S(String(keys[i])));
		cs::Item it;
		if (id && !m.count(*id) && pack_item(String(md[keys[i]]), cs::Kind::Model, *id, it)) m[*id] = it.hash;
	}
	return m;
}

std::vector<cs::Item> JVTextureLoader::build_offer() {
	std::vector<cs::Item> v;
	auto add = [&](uint16_t id, cs::Kind k, const String &dir) {
		if (v.size() >= cs::kMaxItems) return;
		for (auto &x : v) if (x.id == id) return;
		cs::Item it;
		if (pack_item(dir, k, id, it)) v.push_back(it);
	};
	for (auto id : equipped_) {
		auto s = skins_.find(id);
		auto r = pack_refs_.find(id);
		if (s == skins_.end() || r == pack_refs_.end()) continue;
		cs::Kind k = s->second.outfit.present ? cs::Kind::Outfit
				: (s->second.hat.present || s->second.face.present || s->second.back.present) ? cs::Kind::Accessory : cs::Kind::Outfit;
		add(id, k, r->second.dir);
	}
	auto pick = [&](const String &which, cs::Kind k) {
		String p = which == "ball" ? picked_ball_ : picked_stage_;
		auto id = sk::parse_id(S(p));
		if (!id || !skins_.count(*id) || skins_[*id].type != which || !pack_refs_.count(*id)) return;
		add(*id, k, pack_refs_[*id].dir);
	};
	pick("ball", cs::Kind::Ball);
	pick("stage", cs::Kind::Court);
	String mid, mdir;
	if (jvm_models_local_offer(mid, mdir))
		if (auto id = sk::parse_id(S(mid))) add(*id, cs::Kind::Model, mdir);
	return v;
}


void JVTextureLoader::send_offer_to(int32_t peer, int32_t about, const std::vector<cs::Item> &items) {
	if (!net_on_ || !sync_open()) return;
	Ref<SceneMultiplayer> sm = get_tree()->get_multiplayer();
	if (sm.is_null()) return;
	cs::Offer o;
	o.relayed = sm->is_server();
	o.peer = about;
	o.items = items;
	std::vector<uint8_t> b = cs::encode(o);
	PackedByteArray pb;
	pb.resize(int64_t(b.size()));
	memcpy(pb.ptrw(), b.data(), b.size());
	sm->send_bytes(pb, peer, MultiplayerPeer::TRANSFER_MODE_RELIABLE, 0);
	offers_sent_++;
}

void JVTextureLoader::on_offer(int32_t from, const cs::Offer &o) {
	Ref<SceneMultiplayer> sm = get_tree()->get_multiplayer();
	const bool host = sm.is_valid() && sm->is_server();
	int32_t about;
	if (host) {
		if (o.relayed || !modded_peers_.count(from)) return;
		about = from;
	} else {
		if (from != 1 || !o.relayed || o.peer == my_peer()) return;
		about = o.peer;
	}
	offers_received_++;
	offers_[about] = o.items;
	jvm::log("textures", "content: " + String::num_int64(about) + " wears " + String::num_int64(int64_t(o.items.size())) + " item(s)");
	if (host) for (int32_t m : modded_peers_) if (m != from) send_offer_to(m, from, o.items);
	{

		std::vector<sk::SkinId> ids;
		for (auto &it : o.items) if (it.kind == cs::Kind::Outfit || it.kind == cs::Kind::Accessory) ids.push_back(it.id);
		auto &rp = remote_[about];
		if (rp.name.empty()) rp.name = S(player_name(about));
		rp.ids = ids;
		update_missing_ui();
	}
	if (!host && about == 1) {
		host_ball_ = ""; host_stage_ = "";
		for (auto &it : o.items) {
			if (it.kind == cs::Kind::Ball) host_ball_ = hex_id(it.id);
			if (it.kind == cs::Kind::Court) host_stage_ = hex_id(it.id);
		}
	}
	consider_offer(about);
	push_remote_models();
}


void JVTextureLoader::consider_offer(int32_t peer) {
	auto it = offers_.find(peer);
	if (it == offers_.end()) return;
	auto inst = installed_hashes();
	auto &st = offer_state_[peer];
	st.clear();
	bool need_catalog = false;
	for (auto &item : it->second) {
		auto in = inst.find(item.id);
		if (in != inst.end()) { st[item.id] = in->second == item.hash ? "have" : cs::decision_name(cs::Decision::LocalDiffers); continue; }
		auto d = dl_state_.find(item.id);
		if (d != dl_state_.end() && !d->second.begins_with("rejected") && !d->second.begins_with("failed")) { st[item.id] = d->second; continue; }
		if (!content_auto_download()) { st[item.id] = "missing (auto download off)"; continue; }
		if (cat_state_ != 2) { st[item.id] = cat_state_ == 3 ? "missing (assets repo unreachable)" : "checking the assets repo"; need_catalog = true; continue; }
		cs::Decision dec = cs::decide(item, inst, catalog_);
		if (dec == cs::Decision::Download) {
			if (d == dl_state_.end()) { dl_state_[item.id] = "queued"; dl_queue_.push_back(item.id); }
			st[item.id] = dl_state_[item.id];
			continue;
		}
		st[item.id] = cs::decision_name(dec);
		if (dec == cs::Decision::HashMismatch) jvm::warn("textures", "content: " + hex_id(item.id) + " from " + String::num_int64(peer) + " rejected (" + cs::decision_name(dec) + ")");
	}
	if (need_catalog && cat_state_ == 0) cat_retry_at_ = 0.0;
}

void JVTextureLoader::start_http(const String &url, int64_t limit, const String &what) {
	if (!cs::url_allowed(S(url), S(content_base()))) {
		jvm::warn("textures", "content: refused URL " + url);
		http_what_ = what;
		_on_http_done(HTTPRequest::RESULT_REDIRECT_LIMIT_REACHED, 0, PackedStringArray(), PackedByteArray());
		return;
	}
	if (!http_) {
		HTTPRequest *h = memnew(HTTPRequest);
		h->set_name("ContentDownload");
		h->set_use_threads(true);
		h->set_timeout(30.0);
		h->set_max_redirects(0);
		add_child(h);
		h->connect("request_completed", callable_mp(this, &JVTextureLoader::_on_http_done));
		http_ = h;
	}
	HTTPRequest *h = Object::cast_to<HTTPRequest>(http_);
	h->set_body_size_limit(limit);
	http_what_ = what;
	Error e = h->request(url);
	if (e != OK) { http_what_ = ""; _on_http_done(HTTPRequest::RESULT_CANT_CONNECT, 0, PackedStringArray(), PackedByteArray()); }
}

void JVTextureLoader::_on_http_done(int64_t result, int64_t code, const PackedStringArray &, const PackedByteArray &body) {
	String what = http_what_;
	http_what_ = "";
	const bool ok = result == HTTPRequest::RESULT_SUCCESS && code == 200;
	if (what == "manifest" || what.is_empty()) {
		if (!ok) {
			cat_state_ = 3;
			cat_error_ = "assets repo unreachable (result " + String::num_int64(result) + ", HTTP " + String::num_int64(code) + "); retry in 60 s";
			cat_retry_at_ = now_s() + 60.0;
			jvm::log("textures", "content: " + cat_error_ + " - installed and cached packs keep working");
			for (auto &[peer, items] : offers_) consider_offer(peer);
			return;
		}
		on_manifest(body);
		return;
	}
	if (what.begins_with("file:")) {
		PackedStringArray bits = what.split(":");
		auto id = bits.size() == 3 ? sk::parse_id(S(bits[1])) : std::optional<uint16_t>{};
		size_t index = bits.size() == 3 ? size_t(bits[2].to_int()) : 0;
		if (!id || !file_dl_active_ || file_dl_id_ != *id) return;
		String err = ok ? finish_file_fetch(*id, index, body)
				: result == HTTPRequest::RESULT_BODY_SIZE_LIMIT_EXCEEDED ? String("rejected: larger than the manifest says (size cap)")
				: result == HTTPRequest::RESULT_REDIRECT_LIMIT_REACHED ? String("rejected: URL is not an allowed raw.githubusercontent.com address")
				: String("failed: download (result ") + String::num_int64(result) + ", HTTP " + String::num_int64(code) + ")";
		if (!err.is_empty()) abort_file_fetch(*id, err);
		return;
	}
	auto id = sk::parse_id(S(what));
	if (!id) return;
	String err = ok ? install_pack(*id, body)
			: result == HTTPRequest::RESULT_BODY_SIZE_LIMIT_EXCEEDED ? String("rejected: larger than the manifest says (size cap)")
			: result == HTTPRequest::RESULT_REDIRECT_LIMIT_REACHED ? String("rejected: URL is not an allowed raw.githubusercontent.com address")
			: String("failed: download (result ") + String::num_int64(result) + ", HTTP " + String::num_int64(code) + ")";
	if (err.is_empty()) {
		dl_state_[*id] = "installed";
		dl_ok_++;
		jvm::log("textures", "content: " + what + " downloaded and installed");
		content_reload(catalog_.items.count(*id) && catalog_.items[*id].kind == cs::Kind::Model);
	} else {
		dl_state_[*id] = err;
		if (err.begins_with("rejected")) dl_rejected_++;
		jvm::warn("textures", "content: " + what + " " + err);
	}
	for (auto &[peer, items] : offers_) consider_offer(peer);
}

void JVTextureLoader::on_manifest(const PackedByteArray &body) {
	Variant v = JSON::parse_string(body.get_string_from_utf8());
	cs::Catalog cat;
	if (v.get_type() != Variant::DICTIONARY) cat.errors.push_back("manifest is not a JSON object");
	else {
		Dictionary m = v;
		std::vector<cs::RawManifestItem> raw;
		Variant items = m.get("items", Variant());
		if (items.get_type() == Variant::ARRAY) {
			Array a = items;
			for (int i = 0; i < a.size() && i < 4096; i++) {
				if (a[i].get_type() != Variant::DICTIONARY) continue;
				Dictionary d = a[i];
				cs::RawManifestItem r;
				auto str = [&](const char *k) { Variant x = d.get(k, Variant()); return x.get_type() == Variant::STRING ? S(String(x)) : std::string(); };
				r.id = str("id"); r.name = str("name"); r.type = str("type"); r.sha256 = str("sha256");
				r.download_url = str("download_url"); r.path = str("path");
				Variant sz = d.get("size", Variant());
				r.size = (sz.get_type() == Variant::INT || sz.get_type() == Variant::FLOAT) ? double(sz) : -1.0;
				Variant fl = d.get("files", Variant());
				if (fl.get_type() == Variant::ARRAY) {
					Array fa = fl;
					for (int k = 0; k < fa.size(); k++) {
						if (fa[k].get_type() != Variant::DICTIONARY) continue;
						Dictionary f = fa[k];
						Variant fs = f.get("size", Variant());
						double fsz = (fs.get_type() == Variant::INT || fs.get_type() == Variant::FLOAT) ? double(fs) : -1.0;
						std::string fpath = S(String(f.get("path", "")));
						r.files.push_back({ fpath, { S(String(f.get("sha256", ""))), fsz } });
						std::string furl = S(String(f.get("download_url", "")));
						if (furl.empty()) furl = S(String(f.get("source_url", "")));
						if (!furl.empty()) r.file_urls[fpath] = furl;
					}
				}
				raw.push_back(r);
			}
		}
		Variant sv = m.get("schema_version", Variant());
		double schema = (sv.get_type() == Variant::INT || sv.get_type() == Variant::FLOAT) ? double(sv) : -1.0;
		cat = cs::validate_manifest(schema, raw, S(content_base()));
	}
	catalog_ = cat;
	cat_state_ = 2;
	cat_error_ = cat.errors.empty() ? String() : G(cat.errors[0]);
	jvm::log("textures", "content: assets repo lists " + String::num_int64(int64_t(cat.items.size())) + " item(s)"
			+ (cat.errors.empty() ? String() : ", skipped " + String::num_int64(int64_t(cat.errors.size())) + ": " + cat_error_));
	for (auto &[peer, items] : offers_) consider_offer(peer);
}

static void remove_tree(const String &dir, const String &inside) {
	if (!dir.begins_with(inside + String("/")) || !DirAccess::dir_exists_absolute(dir)) return;
	PackedStringArray f = DirAccess::get_files_at(dir);
	for (int i = 0; i < f.size(); i++) DirAccess::remove_absolute(dir.path_join(f[i]));
	PackedStringArray d = DirAccess::get_directories_at(dir);
	for (int i = 0; i < d.size(); i++) remove_tree(dir.path_join(d[i]), inside);
	DirAccess::remove_absolute(dir);
}

String JVTextureLoader::install_pack(uint16_t id, const PackedByteArray &zip) {
	auto ci_it = catalog_.items.find(id);
	if (ci_it == catalog_.items.end()) return "rejected: not in the manifest";
	const cs::CatalogItem &ci = ci_it->second;
	if (uint64_t(zip.size()) != ci.zip_size || uint64_t(zip.size()) > cs::kMaxPackBytes) return "rejected: size mismatch (pack.zip)";
	if (cs::sha256(zip.ptr(), size_t(zip.size())) != ci.zip_sha) return "rejected: hash mismatch (pack.zip is not the manifest's)";
	if (!cs::safe_repo_path("x/" + ci.folder)) return "rejected: unsafe folder";
	const String croot = content_cache_dir();
	const String tmp = croot.path_join(".tmp");
	DirAccess::make_dir_recursive_absolute(tmp);
	const String zpath = tmp.path_join(hex_id(id) + ".zip");
	{
		Ref<FileAccess> f = FileAccess::open(zpath, FileAccess::WRITE);
		if (f.is_null()) return "failed: cannot write the cache";
		f->store_buffer(zip);
	}
	std::map<std::string, cs::FileEntry> want;
	for (auto &f : ci.files) want[f.path] = f;
	const String part = croot.path_join("." + G(ci.folder) + ".part");
	remove_tree(part, croot);
	String err;
	{
		Ref<ZIPReader> zr;
		zr.instantiate();
		if (zr->open(zpath) != OK) err = "rejected: not a zip";
		else {
			PackedStringArray names = zr->get_files();
			if (names.size() > int64_t(cs::kMaxEntries)) err = "rejected: too many entries";
			std::set<std::string> seen;
			uint64_t total = 0;
			for (int i = 0; err.is_empty() && i < names.size(); i++) {
				std::string n = S(names[i]);
				auto safe = cs::safe_entry(n);
				if (!safe) { err = "rejected: unsafe entry '" + names[i].left(64) + "'"; break; }
				if (safe->empty()) continue;
				auto w = want.find(*safe);
				if (w == want.end()) { err = "rejected: '" + names[i] + "' is not in the manifest"; break; }
				std::string lower = n;
				for (auto &ch : lower) ch = char(std::tolower(static_cast<unsigned char>(ch)));
				if (!seen.insert(lower).second) { err = "rejected: duplicate entry"; break; }
				PackedByteArray data = zr->read_file(names[i]);
				total += uint64_t(data.size());
				if (uint64_t(data.size()) != w->second.size || uint64_t(data.size()) > cs::kMaxFileBytes || total > cs::kMaxUnpackedBytes) { err = "rejected: size mismatch for '" + names[i] + "'"; break; }
				if (cs::sha256(data.ptr(), size_t(data.size())) != w->second.sha) { err = "rejected: hash mismatch for '" + names[i] + "'"; break; }
				{
					std::string bad = jvm::safe::check_content_file(*safe, data.ptr(), size_t(data.size()));
					if (!bad.empty()) { err = "rejected: '" + names[i] + "' " + G(bad); jvm::safe::note_refusal("download " + hex_id(id) + ": " + err); break; }
				}
				String out = part.path_join(names[i]);
				DirAccess::make_dir_recursive_absolute(out.get_base_dir());
				Ref<FileAccess> f = FileAccess::open(out, FileAccess::WRITE);
				if (f.is_null()) { err = "failed: cannot write the cache"; break; }
				f->store_buffer(data);
			}
			if (err.is_empty() && seen.size() != want.size()) err = "rejected: files missing from pack.zip";
			zr->close();
		}
	}
	DirAccess::remove_absolute(zpath);
	if (!err.is_empty()) { remove_tree(part, croot); return err; }
	const String final_dir = croot.path_join(G(ci.folder));
	remove_tree(final_dir, croot);
	if (DirAccess::rename_absolute(part, final_dir) != OK) { remove_tree(part, croot); return "failed: cannot move into the cache"; }
	return "";
}


void JVTextureLoader::start_file_fetch(uint16_t id) {
	auto ci_it = catalog_.items.find(id);
	if (ci_it == catalog_.items.end() || !cs::files_have_urls(ci_it->second)) {
		dl_state_[id] = "rejected: no per-file download URLs";
		dl_rejected_++;
		return;
	}
	const cs::CatalogItem &ci = ci_it->second;
	if (!cs::safe_repo_path("x/" + ci.folder)) { dl_state_[id] = "rejected: unsafe folder"; dl_rejected_++; return; }
	const String proot = content_packs_dir();
	DirAccess::make_dir_recursive_absolute(proot);
	const String part = proot.path_join("." + G(ci.folder) + ".part");
	remove_tree(part, proot);
	DirAccess::make_dir_recursive_absolute(part);
	file_dl_id_ = id;
	file_dl_index_ = 0;
	file_dl_part_ = part;
	file_dl_active_ = true;
	const auto &f = ci.files[0];
	start_http(G(f.url), int64_t(std::min(f.size, cs::kMaxFileBytes)), "file:" + hex_id(id) + ":0");
}

void JVTextureLoader::abort_file_fetch(uint16_t id, const String &err) {
	const String proot = content_packs_dir();
	if (!file_dl_part_.is_empty()) remove_tree(file_dl_part_, proot);
	file_dl_active_ = false;
	file_dl_part_ = "";
	file_dl_index_ = 0;
	dl_state_[id] = err;
	if (err.begins_with("rejected")) dl_rejected_++;
	jvm::warn("textures", "content: " + hex_id(id) + " " + err);
	for (auto &[peer, items] : offers_) consider_offer(peer);
}

String JVTextureLoader::finish_file_fetch(uint16_t id, size_t index, const PackedByteArray &body) {
	auto ci_it = catalog_.items.find(id);
	if (ci_it == catalog_.items.end()) return "rejected: not in the manifest";
	const cs::CatalogItem &ci = ci_it->second;
	if (index != file_dl_index_ || index >= ci.files.size()) return "rejected: unexpected file";
	const cs::FileEntry &want = ci.files[index];
	auto safe = cs::safe_entry(want.path);
	if (!safe || *safe != want.path) return "rejected: unsafe entry '" + G(want.path) + "'";
	if (uint64_t(body.size()) != want.size || uint64_t(body.size()) > cs::kMaxFileBytes) return "rejected: size mismatch for '" + G(want.path) + "'";
	if (cs::sha256(body.ptr(), size_t(body.size())) != want.sha) return "rejected: hash mismatch for '" + G(want.path) + "'";
	{
		std::string bad = jvm::safe::check_content_file(*safe, body.ptr(), size_t(body.size()));
		if (!bad.empty()) { String err = "rejected: '" + G(want.path) + "' " + G(bad); jvm::safe::note_refusal("download " + hex_id(id) + ": " + err); return err; }
	}
	const String proot = content_packs_dir();
	if (!file_dl_part_.begins_with(proot + String("/"))) return "rejected: unsafe destination";
	String out = file_dl_part_.path_join(G(want.path));
	DirAccess::make_dir_recursive_absolute(out.get_base_dir());
	{
		Ref<FileAccess> f = FileAccess::open(out, FileAccess::WRITE);
		if (f.is_null()) return "failed: cannot write the pack";
		f->store_buffer(body);
	}
	file_dl_index_++;
	if (file_dl_index_ < ci.files.size()) {
		const auto &next = ci.files[file_dl_index_];
		start_http(G(next.url), int64_t(std::min(next.size, cs::kMaxFileBytes)), "file:" + hex_id(id) + ":" + String::num_int64(int64_t(file_dl_index_)));
		return "";
	}
	const String final_dir = proot.path_join(G(ci.folder));
	remove_tree(final_dir, proot);
	if (DirAccess::rename_absolute(file_dl_part_, final_dir) != OK) { remove_tree(file_dl_part_, proot); return "failed: cannot move into packs"; }
	file_dl_active_ = false;
	file_dl_part_ = "";
	file_dl_index_ = 0;
	dl_state_[id] = "installed";
	dl_ok_++;
	jvm::log("textures", "content: " + hex_id(id) + " downloaded and installed into packs/" + G(ci.folder));
	content_reload(ci.kind == cs::Kind::Model);
	for (auto &[peer, items] : offers_) consider_offer(peer);
	return "";
}

void JVTextureLoader::content_reload(bool models) {
	load_packs(false);
	apply_scene_cosmetics(true);
	refresh_players();
	update_missing_ui();
	if (models) jvm_models_rescan();
	push_remote_models();
}

void JVTextureLoader::push_remote_models() {
	Dictionary d;
	if (net_on_)
		for (auto &[peer, items] : offers_)
			for (auto &it : items) if (it.kind == cs::Kind::Model) d[peer] = hex_id(it.id);
	jvm_models_set_remote(d);
}

String JVTextureLoader::shown_pick(const String &which) const {
	String own = which == "ball" ? picked_ball_ : picked_stage_;
	if (!net_on_ || !offers_.count(1)) return own;
	Ref<SceneMultiplayer> sm = const_cast<JVTextureLoader *>(this)->get_tree()->get_multiplayer();
	if (sm.is_null() || sm->is_server()) return own;

	String h = which == "ball" ? host_ball_ : host_stage_;
	auto id = sk::parse_id(S(h));
	if (id && skins_.count(*id) && skins_.at(*id).type == which) return h;
	return "";
}

void JVTextureLoader::content_net_stop() {
	offers_.clear();
	offer_state_.clear();
	sent_offer_.clear();
	offer_sent_ = false;
	host_ball_ = ""; host_stage_ = "";
	dl_queue_.clear();
	for (auto it = dl_state_.begin(); it != dl_state_.end();) {
		if (it->second == "queued" || it->second.begins_with("failed")) it = dl_state_.erase(it); else ++it;
	}
	push_remote_models();
}

void JVTextureLoader::content_tick() {

	String sb = shown_pick("ball"), ss = shown_pick("stage");
	if (sb != shown_ball_ || ss != shown_stage_) {
		bool first = shown_ball_.is_empty() && shown_stage_.is_empty() && sb == picked_ball_ && ss == picked_stage_;
		shown_ball_ = sb; shown_stage_ = ss;
		if (!first) apply_scene_cosmetics(true);
	}
	if (!net_on_ || !sync_open()) return;
	Ref<SceneMultiplayer> sm = get_tree()->get_multiplayer();
	if (sm.is_null()) return;
	const bool host = sm->is_server();
	std::vector<cs::Item> cur = build_offer();
	if (!offer_sent_ || cur != sent_offer_) {
		bool sent = false;
		if (host) { for (int32_t m : modded_peers_) send_offer_to(m, my_peer(), cur); sent = true; }
		else if (welcomed_) { send_offer_to(1, 0, cur); sent = true; }
		if (sent) {
			if (offer_sent_) jvm::log("textures", "content: cosmetics changed -> offer re-sent (" + String::num_int64(int64_t(cur.size())) + " item(s))");
			sent_offer_ = cur;
			offer_sent_ = true;
		}
	}
	if (!http_what_.is_empty()) return;
	bool waiting = false;
	for (auto &[peer, st] : offer_state_) for (auto &[id, s] : st) if (s == "checking the assets repo" || s.begins_with("missing (assets repo")) waiting = true;
	if (waiting && (cat_state_ == 0 || (cat_state_ == 3 && now_s() >= cat_retry_at_))) {
		cat_state_ = 1;
		start_http(content_base() + "manifest.json", int64_t(cs::kMaxManifestBytes), "manifest");
		return;
	}
	while (!dl_queue_.empty()) {
		uint16_t id = dl_queue_.front();
		dl_queue_.pop_front();
		auto ci = catalog_.items.find(id);
		if (ci == catalog_.items.end() || dl_state_[id] != "queued") continue;
		dl_state_[id] = "downloading";
		if (cs::files_have_urls(ci->second)) start_file_fetch(id);
		else start_http(G(ci->second.url), int64_t(ci->second.zip_size), hex_id(id));
		break;
	}
}

Dictionary JVTextureLoader::get_content_status() const {
	Dictionary s;
	auto item_dict = [](const cs::Item &it) {
		Dictionary d;
		d["id"] = hex_id(it.id); d["kind"] = cs::kind_name(it.kind); d["sha256"] = G(cs::to_hex(it.hash)); d["size"] = int64_t(it.size);
		return d;
	};
	s["sharing"] = net_on_ ? "on" : "off";
	s["repo"] = content_base();
	s["auto_download"] = content_auto_download();
	Array mine;
	for (auto &it : sent_offer_) mine.push_back(item_dict(it));
	s["offer"] = mine;
	Dictionary peers;
	for (auto &[peer, items] : offers_) {
		Array a;
		auto st = offer_state_.find(peer);
		for (auto &it : items) {
			Dictionary d = item_dict(it);
			if (st != offer_state_.end() && st->second.count(it.id)) d["state"] = st->second.at(it.id);
			a.push_back(d);
		}
		peers[String::num_int64(peer)] = a;
	}
	s["peers"] = peers;
	static const char *cst[] = { "not fetched", "fetching", "ready", "offline" };
	Dictionary cat;
	cat["state"] = cst[cat_state_ & 3];
	cat["items"] = int64_t(catalog_.items.size());
	Array errs;
	for (auto &e : catalog_.errors) errs.push_back(G(e));
	cat["errors"] = errs;
	if (!cat_error_.is_empty()) cat["error"] = cat_error_;
	s["catalog"] = cat;
	Dictionary dl;
	for (auto &[id, st] : dl_state_) dl[hex_id(id)] = st;
	s["downloads"] = dl;
	s["downloaded"] = dl_ok_;
	s["rejected"] = dl_rejected_;
	s["offers_sent"] = offers_sent_;
	s["offers_received"] = offers_received_;
	s["shown_ball"] = shown_ball_;
	s["shown_stage"] = shown_stage_;
	Array cached;
	for (auto &[id, ref] : pack_refs_) if (ref.cached) cached.push_back(hex_id(id));
	s["cached"] = cached;
	return s;
}

bool JVTextureLoader::content_fetch(const String &id_s) {
	auto id = sk::parse_id(S(id_s));
	if (!id || !net_on_ || !sync_open() || cat_state_ != 2 || !catalog_.items.count(*id)) return false;
	auto st = dl_state_.find(*id);
	if (st != dl_state_.end() && (st->second == "queued" || st->second == "downloading" || st->second == "installed")) return true;
	if (installed_hashes().count(*id)) return true;
	dl_state_[*id] = "queued";
	dl_queue_.push_back(*id);
	return true;
}
