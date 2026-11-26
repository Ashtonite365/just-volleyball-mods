#include "content_share.hpp"
#include "safe_content.hpp"
#include <fstream>
#include <map>
#ifdef _WIN32
#include <direct.h>
#define JV_MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#include <sys/types.h>
#define JV_MKDIR(p) ::mkdir((p), 0755)
#endif

#include <algorithm>
#include <cmath>
#include <cstring>

namespace jvm::content {


static const uint32_t K[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };
static inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

Sha256::Sha256() {
	const uint32_t init[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
	std::memcpy(h_, init, sizeof h_);
}
void Sha256::block(const uint8_t *p) {
	uint32_t w[64];
	for (int i = 0; i < 16; i++) w[i] = uint32_t(p[4 * i]) << 24 | uint32_t(p[4 * i + 1]) << 16 | uint32_t(p[4 * i + 2]) << 8 | p[4 * i + 3];
	for (int i = 16; i < 64; i++) {
		uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
		uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}
	uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
	for (int i = 0; i < 64; i++) {
		uint32_t t1 = h + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
		uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
		h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
	}
	h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d; h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += h;
}
void Sha256::update(const uint8_t *data, size_t n) {
	len_ += n;
	while (n) {
		size_t take = std::min(n, size_t(64) - used_);
		std::memcpy(buf_ + used_, data, take);
		used_ += take; data += take; n -= take;
		if (used_ == 64) { block(buf_); used_ = 0; }
	}
}
Hash Sha256::finish() {
	uint64_t bits = len_ * 8;
	uint8_t pad = 0x80;
	update(&pad, 1);
	uint8_t z = 0;
	while (used_ != 56) update(&z, 1);
	uint8_t l[8];
	for (int i = 0; i < 8; i++) l[i] = uint8_t(bits >> (56 - 8 * i));
	update(l, 8);
	Hash out;
	for (int i = 0; i < 8; i++) for (int k = 0; k < 4; k++) out[4 * i + k] = uint8_t(h_[i] >> (24 - 8 * k));
	return out;
}
Hash sha256(const uint8_t *data, size_t n) { Sha256 s; s.update(data, n); return s.finish(); }

std::string to_hex(const Hash &h) {
	static const char *x = "0123456789abcdef";
	std::string s(64, '0');
	for (int i = 0; i < 32; i++) { s[2 * i] = x[h[i] >> 4]; s[2 * i + 1] = x[h[i] & 15]; }
	return s;
}
std::optional<Hash> from_hex(const std::string &s) {
	if (s.size() != 64) return std::nullopt;
	Hash h;
	for (int i = 0; i < 64; i++) {
		char c = s[i]; int v;
		if (c >= '0' && c <= '9') v = c - '0';
		else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
		else return std::nullopt;
		if (i % 2 == 0) h[i / 2] = uint8_t(v << 4); else h[i / 2] |= uint8_t(v);
	}
	return h;
}

Hash tree_hash(std::vector<FileEntry> files) {
	std::sort(files.begin(), files.end(), [](const FileEntry &a, const FileEntry &b) { return a.path < b.path; });
	Sha256 s;
	for (auto &f : files) {
		std::string line = f.path + "\n" + to_hex(f.sha) + "\n" + std::to_string(f.size) + "\n";
		s.update(reinterpret_cast<const uint8_t *>(line.data()), line.size());
	}
	return s.finish();
}
uint64_t tree_size(const std::vector<FileEntry> &files) { uint64_t n = 0; for (auto &f : files) n += f.size; return n; }


const char *kind_name(Kind k) {
	switch (k) { case Kind::Outfit: return "outfit"; case Kind::Accessory: return "accessory"; case Kind::Ball: return "ball"; case Kind::Model: return "model"; case Kind::Court: return "court"; }
	return "?";
}
std::optional<Kind> kind_from_type(const std::string &t) {
	if (t == "ball") return Kind::Ball;
	if (t == "outfit" || t == "skin") return Kind::Outfit;
	if (t == "hat" || t == "face" || t == "back" || t == "accessory") return Kind::Accessory;
	if (t == "model") return Kind::Model;
	if (t == "stage" || t == "court") return Kind::Court;
	return std::nullopt;
}

static void put_u16(std::vector<uint8_t> &b, uint16_t v) { b.push_back(v & 0xFF); b.push_back(v >> 8); }
static void put_u32(std::vector<uint8_t> &b, uint32_t v) { for (int i = 0; i < 4; i++) b.push_back((v >> (8 * i)) & 0xFF); }

std::vector<uint8_t> encode(const Offer &o) {
	std::vector<uint8_t> b = { 'J', 'V', 'M', 'S', 1, o.relayed ? kTypeOfferOf : kTypeOffer };
	if (o.relayed) put_u32(b, uint32_t(o.peer));
	size_t n = std::min(o.items.size(), kMaxItems);
	b.push_back(uint8_t(n));
	for (size_t i = 0; i < n; i++) {
		const Item &it = o.items[i];
		put_u16(b, it.id);
		b.push_back(uint8_t(it.kind));
		b.insert(b.end(), it.hash.begin(), it.hash.end());
		put_u32(b, it.size);
	}
	return b;
}

std::optional<Offer> decode(const std::vector<uint8_t> &b) {
	if (b.size() < 7 || std::memcmp(b.data(), "JVMS", 4) != 0 || b[4] != 1) return std::nullopt;
	if (b[5] != kTypeOffer && b[5] != kTypeOfferOf) return std::nullopt;
	Offer o;
	o.relayed = b[5] == kTypeOfferOf;
	size_t i = 6;
	auto need = [&](size_t n) { return i + n <= b.size(); };
	auto u32 = [&]() { uint32_t v = 0; for (int k = 0; k < 4; k++) v |= uint32_t(b[i + k]) << (8 * k); i += 4; return v; };
	if (o.relayed) { if (!need(4)) return std::nullopt; o.peer = int32_t(u32()); }
	if (!need(1)) return std::nullopt;
	size_t n = b[i++];
	if (n > kMaxItems || !need(n * 39)) return std::nullopt;
	for (size_t k = 0; k < n; k++) {
		Item it;
		it.id = uint16_t(b[i] | (b[i + 1] << 8)); i += 2;
		uint8_t kind = b[i++];
		if (kind < 1 || kind > 5) return std::nullopt;
		it.kind = Kind(kind);
		std::memcpy(it.hash.data(), b.data() + i, 32); i += 32;
		it.size = u32();
		o.items.push_back(it);
	}
	if (i != b.size()) return std::nullopt;
	return o;
}


static bool plain_char(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.'; }

static bool url_rest_ok(const std::string &rest) {
	if (rest.empty() || rest.size() > 200 || rest.find("..") != std::string::npos || rest.find("//") != std::string::npos) return false;
	for (char c : rest) if (!plain_char(c) && c != '/') return false;
	return true;
}
static bool is_commit_sha(const std::string &s) {
	if (s.size() != 40) return false;
	for (char c : s) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
	return true;
}
bool url_allowed(const std::string &url, const std::string &base) {
	const std::string prefix = kRepoPrefix;
	const bool official_base = base.empty() || base == kRawBase || base == prefix;
	if (official_base && url.size() > prefix.size() && url.compare(0, prefix.size(), prefix) == 0) {
		std::string rest = url.substr(prefix.size());
		auto slash = rest.find('/');
		if (slash == std::string::npos || slash == 0) return false;
		std::string ref = rest.substr(0, slash);
		if (ref != "main" && !is_commit_sha(ref)) return false;
		return url_rest_ok(rest.substr(slash + 1));
	}
	if (base.empty() || url.size() <= base.size() || url.compare(0, base.size(), base) != 0) return false;
	return url_rest_ok(url.substr(base.size()));
}

bool safe_repo_path(const std::string &p) {
	if (p.empty() || p.size() > 120 || p.front() == '/' || p.back() == '/' || p.find("..") != std::string::npos || p.find("//") != std::string::npos) return false;
	int slashes = 0;
	for (char c : p) { if (c == '/') slashes++; else if (!plain_char(c)) return false; }
	if (slashes < 1 || slashes > 2) return false;
	std::string last = cache_folder(p);
	return !last.empty() && last != "." && last[0] != '.';
}
std::string cache_folder(const std::string &p) { size_t s = p.rfind('/'); return s == std::string::npos ? p : p.substr(s + 1); }

static std::optional<uint16_t> parse_id4(const std::string &s) {
	if (s.size() != 4) return std::nullopt;
	unsigned v = 0;
	for (char c : s) {
		int d;
		if (c >= '0' && c <= '9') d = c - '0'; else if (c >= 'a' && c <= 'f') d = c - 'a' + 10; else if (c >= 'A' && c <= 'F') d = c - 'A' + 10; else return std::nullopt;
		v = v * 16 + unsigned(d);
	}
	return uint16_t(v);
}
static bool whole(double v, double lo, double hi) { return std::isfinite(v) && v >= lo && v <= hi && std::floor(v) == v; }

Catalog validate_manifest(double schema, const std::vector<RawManifestItem> &raw, const std::string &base) {
	Catalog c;
	if (!(schema == double(kSchemaVersion))) { c.errors.push_back("unsupported schema_version (need 1)"); return c; }
	for (const auto &r : raw) {
		auto bad = [&](const std::string &why) { c.errors.push_back("item '" + r.id + "': " + why); };
		auto id = parse_id4(r.id);
		if (!id) { bad("id must be 4 hex digits"); continue; }
		auto kind = kind_from_type(r.type);
		if (!kind) { bad("type '" + r.type + "' is not a cosmetic"); continue; }
		auto zsha = from_hex(r.sha256);
		if (!zsha) { bad("sha256 must be 64 hex digits"); continue; }
		if (!whole(r.size, 1, double(kMaxPackBytes))) { bad("size must be 1 .. " + std::to_string(kMaxPackBytes) + " bytes"); continue; }
		if (!safe_repo_path(r.path)) { bad("unsafe path"); continue; }
		const std::string zip_suffix = r.path + "/pack.zip";
		const std::string url = r.download_url;
		const std::string default_url = base + zip_suffix;
		if (!url_allowed(url, base) || url.size() < zip_suffix.size() || url.compare(url.size() - zip_suffix.size(), zip_suffix.size(), zip_suffix) != 0)
			{ bad("download_url must be a raw URL for " + zip_suffix); continue; }
		if (base != kRawBase && url != default_url) { bad("download_url must be " + default_url); continue; }
		if (r.files.empty() || r.files.size() > kMaxEntries) { bad("files: 1 .. " + std::to_string(kMaxEntries) + " entries"); continue; }
		CatalogItem ci;
		bool ok = true;
		uint64_t total = 0;
		for (auto &[fp, fv] : r.files) {
			auto safe = safe_entry(fp);
			auto fsha = from_hex(fv.first);
			if (!safe || safe->empty() || *safe != fp) { bad("file '" + fp + "': unsafe name or type"); ok = false; break; }
			if (!fsha || !whole(fv.second, 0, double(kMaxFileBytes))) { bad("file '" + fp + "': bad sha256 / size"); ok = false; break; }
			total += uint64_t(fv.second);
			std::string furl;
			auto uit = r.file_urls.find(fp);
			if (uit != r.file_urls.end()) furl = uit->second;
			if (!furl.empty()) {
				if (!url_allowed(furl, base)) { bad("file '" + fp + "': download_url is not an allowed raw URL"); ok = false; break; }
				if (furl.size() < fp.size() + 1 || furl.compare(furl.size() - fp.size() - 1, fp.size() + 1, "/" + fp) != 0
						|| furl.find("/" + r.path + "/") == std::string::npos) {
					bad("file '" + fp + "': download_url must end with /" + r.path + "/" + fp); ok = false; break;
				}
			}
			ci.files.push_back({ fp, *fsha, uint64_t(fv.second), furl });
		}
		if (!ok) continue;
		if (total > kMaxUnpackedBytes) { bad("files too large in total"); continue; }
		if (c.items.count(*id)) { bad("duplicate id (first kept)"); continue; }
		ci.id = *id; ci.name = r.name; ci.type = r.type; ci.kind = *kind;
		ci.zip_sha = *zsha; ci.zip_size = uint64_t(r.size); ci.url = url; ci.folder = cache_folder(r.path);
		ci.tree = tree_hash(ci.files); ci.tree_bytes = total;
		c.items[*id] = ci;
	}
	return c;
}


bool allowed_ext(const std::string &p) {
	size_t d = p.rfind('.');
	if (d == std::string::npos) return false;
	std::string e = p.substr(d + 1);
	for (auto &ch : e) ch = char(std::tolower(static_cast<unsigned char>(ch)));
	return e == "png" || e == "json" || e == "glb";
}

std::optional<std::string> safe_entry(const std::string &name) {
	if (name.empty() || name.size() > 96) return std::nullopt;
	if (name.back() == '/') {
		std::string d = name.substr(0, name.size() - 1);
		if (d.empty()) return std::nullopt;
		std::string probe = d + "/x.png";
		return safe_entry(probe) ? std::optional<std::string>("") : std::nullopt;
	}
	if (name[0] == '/' || name.find('\\') != std::string::npos || name.find(':') != std::string::npos) return std::nullopt;
	int depth = 0;
	size_t start = 0;
	while (true) {
		size_t s = name.find('/', start);
		std::string seg = name.substr(start, s == std::string::npos ? std::string::npos : s - start);
		if (seg.empty() || seg[0] == '.' || seg.back() == ' ') return std::nullopt;
		for (char c : seg) if (!plain_char(c) && c != ' ' && c != '+' && c != '(' && c != ')') return std::nullopt;
		if (seg.find("..") != std::string::npos) return std::nullopt;
		if (++depth > 3) return std::nullopt;
		if (s == std::string::npos) break;
		start = s + 1;
	}
	if (!allowed_ext(name)) return std::nullopt;
	return name;
}


const char *decision_name(Decision d) {
	switch (d) {
		case Decision::Have: return "have it";
		case Decision::Download: return "download";
		case Decision::NotInCatalog: return "not in the assets repo";
		case Decision::HashMismatch: return "hash mismatch: the peer's item is not the repo's";
		case Decision::LocalDiffers: return "a different local pack with this id is installed (kept)";
		case Decision::TooBig: return "too large";
	}
	return "?";
}
Decision decide(const Item &o, const std::map<uint16_t, Hash> &installed, const Catalog &cat) {
	auto in = installed.find(o.id);
	if (in != installed.end()) return in->second == o.hash ? Decision::Have : Decision::LocalDiffers;
	auto ci = cat.items.find(o.id);
	if (ci == cat.items.end()) return Decision::NotInCatalog;
	if (ci->second.tree != o.hash || ci->second.tree_bytes != o.size || ci->second.kind != o.kind) return Decision::HashMismatch;
	if (ci->second.zip_size > kMaxPackBytes || ci->second.tree_bytes > kMaxUnpackedBytes) return Decision::TooBig;
	return Decision::Download;
}

bool files_have_urls(const CatalogItem &ci) {
	if (ci.files.empty()) return false;
	for (const auto &f : ci.files) if (f.url.empty()) return false;
	return true;
}

std::string install_verified_files(const std::string &dest, const CatalogItem &ci,
		const std::vector<std::pair<std::string, std::vector<uint8_t>>> &bodies) {
	if (dest.empty() || dest.find("..") != std::string::npos) return "rejected: unsafe destination";
	if (bodies.size() != ci.files.size()) return "rejected: files missing or extra";
	std::map<std::string, const FileEntry *> want;
	for (const auto &f : ci.files) want[f.path] = &f;
	uint64_t total = 0;
	for (const auto &b : bodies) {
		auto safe = safe_entry(b.first);
		if (!safe || *safe != b.first) return "rejected: unsafe entry '" + b.first + "'";
		auto w = want.find(b.first);
		if (w == want.end()) return "rejected: '" + b.first + "' is not in the manifest";
		total += uint64_t(b.second.size());
		if (uint64_t(b.second.size()) != w->second->size || uint64_t(b.second.size()) > kMaxFileBytes || total > kMaxUnpackedBytes)
			return "rejected: size mismatch for '" + b.first + "'";
		if (sha256(b.second.data(), b.second.size()) != w->second->sha)
			return "rejected: hash mismatch for '" + b.first + "'";
		std::string bad = jvm::safe::check_content_file(*safe, b.second.data(), b.second.size());
		if (!bad.empty()) return "rejected: '" + b.first + "' " + bad;
		std::string out = dest + "/" + b.first;
		{
			std::string dir = out;
			auto slash = dir.rfind('/');
			if (slash != std::string::npos) {
				dir.resize(slash);
				std::string sofar;
				for (size_t i = 0; i < dir.size(); i++) {
					sofar.push_back(dir[i]);
					if (dir[i] == '/' || i + 1 == dir.size()) {
						if (sofar.empty() || sofar == "/" || sofar == dest) continue;
						JV_MKDIR(sofar.c_str());
					}
				}
			}
		}
		std::ofstream f(out, std::ios::binary);
		if (!f) return "failed: cannot write '" + b.first + "'";
		f.write(reinterpret_cast<const char *>(b.second.data()), std::streamsize(b.second.size()));
		if (!f) return "failed: cannot write '" + b.first + "'";
	}
	return {};
}

}
